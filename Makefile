CC = i686-w64-mingw32-gcc
WINDRES = i686-w64-mingw32-windres
CFLAGS = -std=c99 -O2 -Wall -Wextra -Werror -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DUNICODE -D_UNICODE -DCJSON_NESTING_LIMIT=32
TLS_DIR = .deps/mbedtls
TLS_LIBS = $(TLS_DIR)/library/libmbedtls.a $(TLS_DIR)/library/libmbedx509.a $(TLS_DIR)/library/libmbedcrypto.a
TLS_FLAGS = -I$(TLS_DIR)/include -Isrc -DMBEDTLS_USER_CONFIG_FILE=\"tls_config.h\"

LDLIBS = -lcomctl32 -lshell32 -lgdi32 -luser32 -lws2_32 -lcrypt32 -ladvapi32 -lcomdlg32

all: build/connect95.exe

build:
	mkdir -p build

build/resources.o: resources/connect95.rc resources/connect95.manifest resources/phone.ico resources/send.ico resources/ring.ico resources/clipboard.ico | build
	$(WINDRES) -I resources $< $@

build/connect95.exe: src/main.c src/saved_devices.c src/clipboard.c src/device_plugins.c src/packet.c src/share_info.c src/transfer.c src/transport.c src/pairing.c src/session.c src/credentials.c src/discovery.c src/identity.c src/vendor/cJSON.c build/resources.o src/discovery.h src/identity.h src/vendor/cJSON.h src/session.h src/saved_devices.h src/clipboard.h src/device_plugins.h src/packet.h src/share_info.h src/transfer.h src/transport.h src/pairing.h src/credentials.h src/tls_config.h $(TLS_LIBS)
	$(CC) $(CFLAGS) $(TLS_FLAGS) -mwindows -static -Wl,--gc-sections -Wl,--major-subsystem-version,5,--minor-subsystem-version,1 -o $@ $(filter %.c %.o,$^) $(TLS_LIBS) $(LDLIBS)

clean:
	rm -rf build

.PHONY: all clean

HOST_CC ?= cc
test: | build
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -DCJSON_NESTING_LIMIT=32 -Isrc tests/identity_test.c src/identity.c src/vendor/cJSON.c -o build/identity-test
	./build/identity-test

.PHONY: test

$(TLS_LIBS) &: src/tls_config.h tools/build-mbedtls.sh
	CC=$(CC) tools/build-mbedtls.sh

.PHONY: tls
tls:
	CC=$(CC) tools/build-mbedtls.sh

.PHONY: test-pairing
test-pairing: | build
	HOST_CC=$(HOST_CC) tests/pairing_test.sh
