#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

SRC_DIR="$ROOT_DIR/sources"
ZLIB_SRC_DIR="$SRC_DIR/zlib"
BUILD_DIR="$ROOT_DIR/build/zlib"
OUT_DIR="$ROOT_DIR/zlib"

ZLIB_URL="https://zlib.net/zlib.tar.gz"
JOBS="$(nproc)"

reset_locale()
{
    export LANG=C
    export LC_ALL=C
    export LC_CTYPE=C
    export LC_MESSAGES=C
}

download_zlib()
{
    mkdir -p "$SRC_DIR"

    if [ -f "$ZLIB_SRC_DIR/configure" ]; then
        echo "zlib sources already exist: $ZLIB_SRC_DIR"
        return
    fi

    echo "Downloading zlib..."
    rm -rf "$SRC_DIR/zlib" "$SRC_DIR/zlib.tar.gz" "$SRC_DIR"/zlib-*

    wget -O "$SRC_DIR/zlib.tar.gz" "$ZLIB_URL"

    tar -xzf "$SRC_DIR/zlib.tar.gz" -C "$SRC_DIR"

    local extracted_dir
    extracted_dir="$(find "$SRC_DIR" -maxdepth 1 -type d -name "zlib-*" | head -n 1)"

    if [ -z "$extracted_dir" ]; then
        echo "ERROR: Cannot find extracted zlib directory"
        exit 1
    fi

    mv "$extracted_dir" "$ZLIB_SRC_DIR"
}

verify_library()
{
    echo
    echo "Architecture check:"

    if [ "$CURRENT_TARGET" = "arm64" ]; then
        aarch64-linux-gnu-objdump -f "$CURRENT_PREFIX/lib/libz.a" | grep '^architecture' || true
    else
        objdump -f "$CURRENT_PREFIX/lib/libz.a" | grep '^architecture' || true
    fi
}

setup_env_lnx64()
{
    reset_locale

    export CMAKE_CXX_COMPILER=/usr/bin/g++
    export CMAKE_C_COMPILER=/usr/bin/gcc
    export CXX=/usr/bin/g++
    export CC=/usr/bin/gcc

    unset AR
    unset RANLIB
    unset LD

    export CURRENT_TARGET="lnx64"
    export CURRENT_PREFIX="$OUT_DIR/lnx64"
    export ZLIB_CONFIGURE_FLAGS="--static --64"
}

setup_env_arm64()
{
    reset_locale

    export CMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++
    export CMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc
    export CXX=/usr/bin/aarch64-linux-gnu-g++
    export CC=/usr/bin/aarch64-linux-gnu-gcc

    export AR=/usr/bin/aarch64-linux-gnu-ar
    export RANLIB=/usr/bin/aarch64-linux-gnu-ranlib
    export LD=/usr/bin/aarch64-linux-gnu-ld

    export CURRENT_TARGET="arm64"
    export CURRENT_PREFIX="$OUT_DIR/arm64"
    export ZLIB_CONFIGURE_FLAGS="--static"
}

check_tools_common()
{
    command -v wget >/dev/null || { echo "ERROR: wget not found"; exit 1; }
    command -v tar >/dev/null || { echo "ERROR: tar not found"; exit 1; }
    command -v make >/dev/null || { echo "ERROR: make not found"; exit 1; }
    command -v objdump >/dev/null || { echo "ERROR: objdump not found"; exit 1; }
}

check_tools_lnx64()
{
    command -v gcc >/dev/null || { echo "ERROR: gcc not found"; exit 1; }
    command -v g++ >/dev/null || { echo "ERROR: g++ not found"; exit 1; }
}

check_tools_arm64()
{
    command -v aarch64-linux-gnu-gcc >/dev/null || {
        echo "ERROR: aarch64-linux-gnu-gcc not found"
        echo "Install it with:"
        echo "sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
        exit 1
    }

    command -v aarch64-linux-gnu-g++ >/dev/null || {
        echo "ERROR: aarch64-linux-gnu-g++ not found"
        echo "Install it with:"
        echo "sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
        exit 1
    }
}

build_zlib()
{
    local target="$CURRENT_TARGET"
    local prefix="$CURRENT_PREFIX"
    local target_build_dir="$BUILD_DIR/$target"

    echo
    echo "========================================"
    echo "Building zlib for $target"
    echo "Prefix: $prefix"
    echo "CC: $CC"
    echo "CXX: $CXX"
    echo "========================================"

    rm -rf "$target_build_dir"
    mkdir -p "$target_build_dir"
    mkdir -p "$prefix"

    cp -a "$ZLIB_SRC_DIR"/. "$target_build_dir"/

    cd "$target_build_dir"

    make distclean 2>/dev/null || true

    ./configure $ZLIB_CONFIGURE_FLAGS --prefix="$prefix"

    make -j"$JOBS"
    make install

    echo
    echo "zlib installed to: $prefix"

    verify_library

    cd "$ROOT_DIR"
}

build_lnx64()
{
    setup_env_lnx64
    check_tools_lnx64
    build_zlib
}

build_arm64()
{
    setup_env_arm64
    check_tools_arm64
    build_zlib
}

select_target()
{
    local target="$1"

    if [ -n "$target" ]; then
        echo "$target"
        return
    fi

    echo "Select build target:"
    echo "  lnx64  - Linux x64"
    echo "  arm64  - Linux ARM64 cross build"
    echo
    echo "If you press Enter or enter an unknown value, both targets will be built."
    echo

    read -r -p "Target [lnx64/arm64/all]: " target

    case "$target" in
        lnx64|arm64|all)
            echo "$target"
            ;;
        *)
            echo "all"
            ;;
    esac
}

main()
{
    local target
    target="$(select_target "${1:-}")"

    reset_locale
    check_tools_common
    download_zlib

    case "$target" in
        lnx64)
            build_lnx64
            ;;
        arm64)
            build_arm64
            ;;
        all)
            build_lnx64
            build_arm64
            ;;
        *)
            build_lnx64
            build_arm64
            ;;
    esac

    reset_locale
    setup_env_lnx64

    echo
    echo "========================================"
    echo "Done"
    echo
    echo "zlib version: $(grep '#define ZLIB_VERSION' "$ZLIB_SRC_DIR/zlib.h" | cut -d'"' -f2)"
    echo
    echo "Built targets:"
    echo "  ✓ lnx64 : $OUT_DIR/lnx64"
    echo "  ✓ arm64 : $OUT_DIR/arm64"
    echo
    echo "Environment restored to lnx64"
    echo "========================================"
}

main "$@"
