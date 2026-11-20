#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
MBEDTLS_SOURCE_DIR=.deps/mbedtls-host CC="${HOST_CC:-cc}" AR=ar tools/build-mbedtls.sh
"${HOST_CC:-cc}" -std=c99 -Wall -Wextra -Werror -O1 -DCJSON_NESTING_LIMIT=32 \
    -DMBEDTLS_USER_CONFIG_FILE='"tls_config.h"' -Isrc -I.deps/mbedtls-host/include \
    tests/pairing_test.c src/pairing.c src/packet.c src/share_info.c src/vendor/cJSON.c \
    .deps/mbedtls-host/library/libmbedtls.a .deps/mbedtls-host/library/libmbedx509.a \
    .deps/mbedtls-host/library/libmbedcrypto.a -o build/pairing-test
fixture_dir=$(mktemp -d)
trap 'rm -rf "$fixture_dir"' EXIT HUP INT TERM
openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 2 -subj /CN=1234567890abcdef1234567890abcdef \
    -keyout "$fixture_dir/a.key" -out "$fixture_dir/a.crt" 2>/dev/null
openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 2 -subj /CN=abcdef1234567890abcdef1234567890 \
    -keyout "$fixture_dir/b.key" -out "$fixture_dir/b.crt" 2>/dev/null
openssl pkey -in "$fixture_dir/a.key" -pubout -outform DER -out "$fixture_dir/a.der"
openssl pkey -in "$fixture_dir/b.key" -pubout -outform DER -out "$fixture_dir/b.der"
expected=$(python3 - "$fixture_dir" <<'PY'
import hashlib
import pathlib
import sys
p = pathlib.Path(sys.argv[1])
a, b = sorted([(p / 'a.der').read_bytes(), (p / 'b.der').read_bytes()], reverse=True)
print(hashlib.sha256(a + b).hexdigest()[:8].upper())
PY
)
./build/pairing-test "$fixture_dir/a.crt" "$fixture_dir/a.key" "$fixture_dir/b.crt" "$fixture_dir/b.key" "$expected"
