# wmon

WiFi monitor mode and 802.11 frame parsing for [Praia](https://praia.sh). Enable monitor mode on Linux, capture raw WiFi frames, and parse radiotap + 802.11 headers.

Requires root and a wireless adapter that supports monitor mode.

## Installation

```sh
sand install github.com/viggou/praia-wmon
```

## Usage

### Manual capture

```praia
use "wmon"
use "pcap"

let mon = wmon.enable("wlan0")
wmon.setChannel(mon, 6)
let cap = pcap.openLive(mon)

for (i in 0..100) {
    let pkt = pcap.next(cap)
    if (!pkt) { continue }

    let rt = wmon.parseRadiotap(pkt.data)
    let frame = wmon.parse80211(pkt.data, rt.headerLen)
    if (!frame) { continue }

    if (frame.typeStr == "beacon") {
        print(frame.ssid, frame.addr3, "ch:", frame.channel,
              "signal:", rt.signal, "dBm", frame.encryption)
    }
}

pcap.close(cap)
wmon.disable(mon)
```

### Quick scan

```praia
use "wmon"
use "pcap"

let aps = wmon.scan("wlan0", {pcap: pcap, dwell: 300})
for (ap in aps) {
    print(ap.ssid, ap.bssid, "ch:", ap.channel,
          "signal:", ap.signal, "dBm", ap.encryption)
}
```

## API

### Parsing (all platforms)

| Function | Description |
|----------|-------------|
| `parseRadiotap(data)` | Parse radiotap header. Returns `{headerLen, channel, signal, noise, rate, flags}` or nil |
| `parse80211(data, offset?)` | Parse 802.11 frame at byte offset. Returns frame info map or nil |

#### `parse80211` return fields

| Field | Description |
|-------|-------------|
| `type` | Frame type: 0=management, 1=control, 2=data |
| `subtype` | Frame subtype number |
| `typeStr` | Human-readable type: `"beacon"`, `"probe-req"`, `"deauth"`, `"data"`, etc. |
| `toDS` / `fromDS` | Direction flags |
| `retry` | Retransmission flag |
| `protected` | Frame is encrypted |
| `addr1` / `addr2` / `addr3` | MAC addresses (receiver, transmitter, BSSID) |
| `seqNum` | Sequence number |
| `ssid` | SSID (beacons/probes only, empty for hidden networks) |
| `channel` | Channel from DS Parameter Set (beacons/probes) |
| `encryption` | `"open"`, `"wep"`, `"wpa"`, or `"wpa2"` (beacons/probes) |

### Interface management (Linux only, requires root)

| Function | Description |
|----------|-------------|
| `enable(iface, monName?)` | Create monitor interface. Returns monitor interface name (default: `<iface>mon`) |
| `disable(monName)` | Remove monitor interface and restore original |
| `setChannel(iface, channel)` | Set channel on monitor interface |

### High-level scan (Linux only)

| Function | Description |
|----------|-------------|
| `scan(iface, opts)` | Scan channels and collect APs. Returns `[{ssid, bssid, channel, signal, encryption}]` |

`scan` options:
- `pcap` — pcap module reference (required)
- `channels` — array of channel numbers (default: 1-13)
- `dwell` — ms per channel (default: 200)

## Building from source

```sh
make
```

## Platform notes

- **Linux**: Full support. Uses `iw` and `ip` commands for interface management. Requires a wireless adapter with monitor mode support, root, and `iw`/`iproute2` installed.
- **macOS**: Parsing functions work (useful for reading pcap files captured on Linux). Monitor mode is not available — Apple removed the public API on modern hardware.
