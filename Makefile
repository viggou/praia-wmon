PRAIA_INCLUDE := $(shell praia --include-path)
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  OUT = plugins/wmon.dylib
  LDFLAGS = -undefined dynamic_lookup
else
  OUT = plugins/wmon-linux-$(shell uname -m).so
  LDFLAGS =
endif

all:
	g++ -std=c++17 -shared -fPIC -I$(PRAIA_INCLUDE) $(LDFLAGS) -o $(OUT) plugins/wmon.cpp

clean:
	rm -f plugins/wmon.dylib plugins/wmon-linux-*.so

.PHONY: all clean
