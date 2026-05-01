#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700

#include "praia_plugin.h"

#include <cstring>
#include <vector>

// ── Helpers ──

static std::string formatMac(const uint8_t* m) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
    return std::string(buf);
}

static uint16_t readLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static uint32_t readLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// Convert WiFi frequency (MHz) to channel number
static int freqToChannel(uint16_t freq) {
    if (freq >= 2412 && freq <= 2472) return (freq - 2407) / 5;
    if (freq == 2484) return 14;
    if (freq >= 5170 && freq <= 5825) return (freq - 5000) / 5;
    if (freq >= 5955 && freq <= 7115) return (freq - 5950) / 5; // 6 GHz
    return 0;
}

// ── Radiotap parsing ──
// Radiotap is always little-endian. Fields are present based on the
// bitmask in the header and have specific alignment requirements.

static Value parseRadiotapImpl(const uint8_t* data, size_t len) {
    if (len < 8) return Value();

    uint16_t headerLen = readLE16(data + 2);
    if (headerLen > len) return Value();

    uint32_t present = readLE32(data + 4);

    // Skip extended present bitmaps (bit 31 = "another bitmap follows")
    size_t offset = 8;
    uint32_t ext = present;
    while (ext & (1u << 31)) {
        if (offset + 4 > headerLen) return Value();
        ext = readLE32(data + offset);
        offset += 4;
    }

    int channel = 0, signal = 0, noise = 0, rate = 0;
    uint8_t flags = 0;

    // Bit 0: TSFT (u64, 8-byte aligned)
    if (present & (1u << 0)) {
        offset = (offset + 7) & ~7u;
        offset += 8;
    }
    // Bit 1: Flags (u8)
    if (present & (1u << 1)) {
        if (offset < headerLen) flags = data[offset];
        offset += 1;
    }
    // Bit 2: Rate (u8, in 500kbps units)
    if (present & (1u << 2)) {
        if (offset < headerLen) rate = data[offset];
        offset += 1;
    }
    // Bit 3: Channel (u16 freq + u16 flags, 2-byte aligned)
    if (present & (1u << 3)) {
        offset = (offset + 1) & ~1u;
        if (offset + 4 <= headerLen) {
            uint16_t freq = readLE16(data + offset);
            channel = freqToChannel(freq);
        }
        offset += 4;
    }
    // Bit 4: FHSS (2 bytes)
    if (present & (1u << 4)) offset += 2;
    // Bit 5: Antenna Signal dBm (s8)
    if (present & (1u << 5)) {
        if (offset < headerLen) signal = static_cast<int8_t>(data[offset]);
        offset += 1;
    }
    // Bit 6: Antenna Noise dBm (s8)
    if (present & (1u << 6)) {
        if (offset < headerLen) noise = static_cast<int8_t>(data[offset]);
        offset += 1;
    }

    auto result = gcNew<PraiaMap>();
    result->entries["headerLen"] = Value(static_cast<int64_t>(headerLen));
    result->entries["channel"] = Value(static_cast<int64_t>(channel));
    result->entries["signal"] = Value(static_cast<int64_t>(signal));
    result->entries["noise"] = Value(static_cast<int64_t>(noise));
    result->entries["rate"] = Value(static_cast<int64_t>(rate));
    result->entries["flags"] = Value(static_cast<int64_t>(flags));
    return Value(result);
}

// ── 802.11 frame parsing ──

static const char* frameTypeStr(int type, int subtype) {
    if (type == 0) { // Management
        switch (subtype) {
            case 0: return "assoc-req";
            case 1: return "assoc-resp";
            case 2: return "reassoc-req";
            case 3: return "reassoc-resp";
            case 4: return "probe-req";
            case 5: return "probe-resp";
            case 8: return "beacon";
            case 9: return "atim";
            case 10: return "disassoc";
            case 11: return "auth";
            case 12: return "deauth";
            case 13: return "action";
            default: return "mgmt";
        }
    }
    if (type == 1) { // Control
        switch (subtype) {
            case 8: return "block-ack-req";
            case 9: return "block-ack";
            case 10: return "ps-poll";
            case 11: return "rts";
            case 12: return "cts";
            case 13: return "ack";
            default: return "ctrl";
        }
    }
    if (type == 2) return "data";
    return "unknown";
}

static Value parse80211Impl(const uint8_t* data, size_t len) {
    if (len < 2) return Value();

    // Frame control (2 bytes)
    uint8_t fc0 = data[0]; // protocol(2), type(2), subtype(4)
    uint8_t fc1 = data[1]; // toDS, fromDS, moreFrag, retry, pwrMgmt, moreData, protected, order

    int type = (fc0 >> 2) & 0x3;
    int subtype = (fc0 >> 4) & 0xF;
    bool toDS = fc1 & 0x01;
    bool fromDS = (fc1 >> 1) & 0x01;
    bool retry = (fc1 >> 3) & 0x01;
    bool isProtected = (fc1 >> 6) & 0x01;

    auto result = gcNew<PraiaMap>();
    result->entries["type"] = Value(static_cast<int64_t>(type));
    result->entries["subtype"] = Value(static_cast<int64_t>(subtype));
    result->entries["typeStr"] = Value(std::string(frameTypeStr(type, subtype)));
    result->entries["toDS"] = Value(toDS);
    result->entries["fromDS"] = Value(fromDS);
    result->entries["retry"] = Value(retry);
    result->entries["protected"] = Value(isProtected);

    // Control frames (type 1) have variable header sizes
    if (type == 1) {
        if (len >= 10)
            result->entries["addr1"] = Value(formatMac(data + 4));
        if ((subtype == 11 || subtype == 8 || subtype == 9 || subtype == 10) && len >= 16)
            result->entries["addr2"] = Value(formatMac(data + 10));
        return Value(result);
    }

    // Management and data frames: 24-byte minimum header
    if (len < 24) return Value(result);

    result->entries["addr1"] = Value(formatMac(data + 4));
    result->entries["addr2"] = Value(formatMac(data + 10));
    result->entries["addr3"] = Value(formatMac(data + 16));

    uint16_t seqctl = readLE16(data + 22);
    result->entries["seqNum"] = Value(static_cast<int64_t>(seqctl >> 4));

    // Management frames: parse tagged parameters for SSID, channel, encryption
    if (type == 0) {
        size_t bodyOffset = 24;

        if (subtype == 8 || subtype == 5)       bodyOffset += 12; // beacon/probe-resp: timestamp(8)+interval(2)+capability(2)
        else if (subtype == 0)                   bodyOffset += 4;  // assoc-req: capability(2)+listen_interval(2)
        else if (subtype == 2)                   bodyOffset += 10; // reassoc-req: capability(2)+listen_interval(2)+current_ap(6)

        // Parse tagged parameters
        size_t pos = bodyOffset;
        while (pos + 2 <= len) {
            uint8_t tagNum = data[pos];
            uint8_t tagLen = data[pos + 1];
            pos += 2;
            if (pos + tagLen > len) break;

            if (tagNum == 0) {
                // SSID — raw bytes (typically UTF-8 but the spec allows any
                // octets). Length 0 = hidden network. We don't filter to
                // printable ASCII because that would silently drop valid
                // non-ASCII names (CJK, emoji, accented characters).
                result->entries["ssid"] = Value(
                    std::string(reinterpret_cast<const char*>(data + pos), tagLen));
            } else if (tagNum == 3 && tagLen == 1) {
                // DS Parameter Set — channel
                result->entries["channel"] = Value(static_cast<int64_t>(data[pos]));
            } else if (tagNum == 48) {
                // RSN Information Element — WPA2/WPA3
                result->entries["encryption"] = Value(std::string("wpa2"));
            } else if (tagNum == 221 && !result->entries.count("encryption")) {
                // Vendor Specific — check for WPA OUI (00:50:f2:01)
                if (tagLen >= 4 && data[pos] == 0x00 && data[pos+1] == 0x50 &&
                    data[pos+2] == 0xf2 && data[pos+3] == 0x01)
                    result->entries["encryption"] = Value(std::string("wpa"));
            }
            pos += tagLen;
        }

        // Fallback: check capability privacy bit for WEP/open
        if (!result->entries.count("encryption") && (subtype == 8 || subtype == 5)) {
            size_t capOffset = 24 + 10;
            if (capOffset + 2 <= len) {
                uint16_t cap = readLE16(data + capOffset);
                result->entries["encryption"] = (cap & 0x0010)
                    ? Value(std::string("wep"))
                    : Value(std::string("open"));
            }
        }
    }

    return Value(result);
}

// ── Plugin registration ──

extern "C" void praia_register(PraiaMap* module) {
    // wmon.parseRadiotap(data) -> {headerLen, channel, signal, noise, rate, flags} or nil
    module->entries["parseRadiotap"] = Value(makeNative("wmon.parseRadiotap", 1,
        [](const std::vector<Value>& args) -> Value {
            if (!args[0].isString())
                throw RuntimeError("wmon.parseRadiotap() requires packet data", 0);
            const auto& data = args[0].asString();
            return parseRadiotapImpl(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        }));

    // wmon.parse80211(data, offset?) -> {type, subtype, typeStr, toDS, fromDS, ...} or nil
    module->entries["parse80211"] = Value(makeNative("wmon.parse80211", -1,
        [](const std::vector<Value>& args) -> Value {
            if (args.empty() || !args[0].isString())
                throw RuntimeError("wmon.parse80211() requires packet data", 0);
            const auto& data = args[0].asString();
            size_t offset = 0;
            if (args.size() > 1 && args[1].isInt())
                offset = static_cast<size_t>(args[1].asInt());
            if (offset >= data.size()) return Value();
            return parse80211Impl(
                reinterpret_cast<const uint8_t*>(data.data()) + offset,
                data.size() - offset);
        }));
}
