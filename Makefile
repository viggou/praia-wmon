PRAIA_INCLUDE := $(shell praia --include-path)
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  OUT = plugins/wmon.dylib
  LDFLAGS = -undefined dynamic_lookup
  # wmon.cpp self-defines _XOPEN_SOURCE / _DARWIN_C_SOURCE before including
  # praia's fiber.h (which uses swapcontext(3) on macOS).
  EXTRA_FLAGS = -Wno-deprecated-declarations
else
  OUT = plugins/wmon-linux-$(shell uname -m).so
  LDFLAGS =
  EXTRA_FLAGS =
endif

all:
	g++ -std=c++17 -shared -fPIC $(EXTRA_FLAGS) -I$(PRAIA_INCLUDE) $(LDFLAGS) -o $(OUT) plugins/wmon.cpp

clean:
	rm -f plugins/wmon.dylib plugins/wmon-linux-*.so

.PHONY: all clean
