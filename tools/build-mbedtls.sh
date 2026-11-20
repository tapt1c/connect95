#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
source_dir=${MBEDTLS_SOURCE_DIR:-.deps/mbedtls}
revision=82e69999f91126589a28b70dd75b69fbcabe2711
if [ ! -d "$source_dir" ]; then
    mkdir -p .deps
    git clone --depth 1 --branch v3.6.8 https://github.com/Mbed-TLS/mbedtls.git "$source_dir"
fi
[ "$(git -C "$source_dir" rev-parse HEAD)" = "$revision" ] || {
    echo 'Unexpected Mbed TLS revision' >&2
    exit 1
}
git -C "$source_dir" submodule update --init --depth 1 framework
if ! cmp -s src/tls_config.h "$source_dir/.connect95-config"; then
    make -C "$source_dir/library" clean
fi
make -C "$source_dir" lib -j2 CC="${CC:-i686-w64-mingw32-gcc}" AR="${AR:-i686-w64-mingw32-ar}" \
    CFLAGS="-O2 -ffunction-sections -fdata-sections -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -I$(pwd)/src -DMBEDTLS_USER_CONFIG_FILE='\"tls_config.h\"'"

cp src/tls_config.h "$source_dir/.connect95-config"
