#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

SRC_DIR="$ROOT_DIR/sources"
BUILD_DIR="$ROOT_DIR/build/openssl"
OUT_DIR="$ROOT_DIR/openssl"
ZLIB_DIR="$ROOT_DIR/zlib"

OPENSSL_SOURCE_URL="https://openssl-library.org/source/"

TARGET=""
OPENSSL_VERSION=""
OPENSSL_RELEASE=""
OPENSSL_EOL=""
OPENSSL_LTS="0"

DEFAULT_OPENSSL_VERSION=""
DEFAULT_OPENSSL_RELEASE=""
DEFAULT_OPENSSL_EOL=""

JOBS="$(nproc)"

reset_locale()
{
    export LANG=C
    export LC_ALL=C
    export LC_CTYPE=C
    export LC_MESSAGES=C
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
    export CURRENT_ZLIB_PREFIX="$ZLIB_DIR/lnx64"

    export PKG_CONFIG_PATH="$CURRENT_ZLIB_PREFIX/lib/pkgconfig"
    export CPPFLAGS="-I$CURRENT_ZLIB_PREFIX/include"
    export LDFLAGS="-L$CURRENT_ZLIB_PREFIX/lib -Wl,-rpath-link,$CURRENT_ZLIB_PREFIX/lib"

    export OPENSSL_CONFIG_TARGET="linux-x86_64"
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
    export CURRENT_ZLIB_PREFIX="$ZLIB_DIR/arm64"

    export PKG_CONFIG_PATH="$CURRENT_ZLIB_PREFIX/lib/pkgconfig"
    export CPPFLAGS="-I$CURRENT_ZLIB_PREFIX/include"
    export LDFLAGS="-L$CURRENT_ZLIB_PREFIX/lib -Wl,-rpath-link,$CURRENT_ZLIB_PREFIX/lib"

    export OPENSSL_CONFIG_TARGET="linux-aarch64"
}

check_tools_common()
{
    command -v wget >/dev/null || { echo "ERROR: wget not found"; exit 1; }
    command -v tar >/dev/null || { echo "ERROR: tar not found"; exit 1; }
    command -v make >/dev/null || { echo "ERROR: make not found"; exit 1; }
    command -v perl >/dev/null || { echo "ERROR: perl not found"; exit 1; }
    command -v python3 >/dev/null || { echo "ERROR: python3 not found"; exit 1; }
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

    command -v aarch64-linux-gnu-objdump >/dev/null || {
        echo "ERROR: aarch64-linux-gnu-objdump not found"
        echo "Install it with:"
        echo "sudo apt install binutils-aarch64-linux-gnu"
        exit 1
    }
}

select_target()
{
    local input_target="$1"

    if [ -z "$input_target" ]; then
        echo
        echo "Select build target:"
        echo "  lnx64  - Linux x64"
        echo "  arm64  - Linux ARM64 cross build"
        echo
        read -r -p "Target [lnx64/arm64] (leave empty to build both): " input_target
    fi

    case "$input_target" in
        "")
            TARGET="all"
            ;;
        lnx64|arm64|all)
            TARGET="$input_target"
            ;;
        *)
            echo
            echo "ERROR: Unknown build target: '$input_target'"
            echo "Supported targets: lnx64, arm64"
            exit 1
            ;;
    esac
}

fetch_openssl_versions()
{
    local html_file="$SRC_DIR/openssl_source.html"

    mkdir -p "$SRC_DIR"

    echo
    echo "Fetching OpenSSL version list..."
    wget -q -O "$html_file" "$OPENSSL_SOURCE_URL"

    OPENSSL_ITEMS="$(python3 - "$html_file" <<'PY'
import re
import sys
from html import unescape

path = sys.argv[1]

with open(path, "r", encoding="utf-8", errors="ignore") as f:
    html = f.read()

rx = re.compile(
    r'(?is)<tr>\s*'
    r'<td>(?P<series>[^<]+)</td>\s*'
    r'<td>\s*<a[^>]*>openssl-(?P<version>[0-9]+\.[0-9]+\.[0-9]+)\.tar\.gz</a>.*?</td>\s*'
    r'<td>[^<]*</td>\s*'
    r'<td>(?P<release>[^<]+)</td>\s*'
    r'<td>(?P<eol>[^<]+)</td>'
)

items = []

for m in rx.finditer(html):
    series = unescape(m.group("series")).strip()
    version = m.group("version").strip()
    release = unescape(m.group("release")).strip()
    eol = unescape(m.group("eol")).strip()
    lts = "1" if "[LTS]" in series else "0"
    items.append((series, version, release, eol, lts))

if not items:
    raise SystemExit("Cannot parse OpenSSL versions")

for item in items:
    print("|".join(item))
PY
)"

    if [ -z "$OPENSSL_ITEMS" ]; then
        echo "ERROR: Cannot parse OpenSSL versions"
        exit 1
    fi
}

version_sort_key()
{
    printf '%s\n' "$1" | awk -F. '{ printf "%d%03d%03d\n", $1, $2, $3 }'
}

detect_latest_lts()
{
    local latest_version=""
    local latest_key=""

    while IFS='|' read -r series version release eol lts; do
        if [ "$lts" = "1" ]; then
            local key
            key="$(version_sort_key "$version")"

            if [ -z "$latest_key" ] || [ "$key" -gt "$latest_key" ]; then
                latest_key="$key"
                latest_version="$version"
                DEFAULT_OPENSSL_RELEASE="$release"
                DEFAULT_OPENSSL_EOL="$eol"
            fi
        fi
    done <<< "$OPENSSL_ITEMS"

    if [ -z "$latest_version" ]; then
        echo "ERROR: Cannot detect latest OpenSSL LTS version"
        exit 1
    fi

    DEFAULT_OPENSSL_VERSION="$latest_version"
}

print_available_openssl_versions()
{
    echo
    echo "Available OpenSSL versions:"

    while IFS='|' read -r series version release eol lts; do
        local tag=""
        if [ "$lts" = "1" ]; then
            tag=" [LTS]"
        fi

        echo "  $version$tag - released: $release, support until: $eol"
    done <<< "$OPENSSL_ITEMS"
}

select_openssl_version()
{
    local input_version="$1"

    fetch_openssl_versions
    detect_latest_lts
    print_available_openssl_versions

    echo
    echo "Default OpenSSL version: $DEFAULT_OPENSSL_VERSION [LTS], support until: $DEFAULT_OPENSSL_EOL"
    echo

    if [ -z "$input_version" ]; then
        read -r -p "Enter OpenSSL version in format X.Y.Z. Press Enter for default $DEFAULT_OPENSSL_VERSION [LTS]: " input_version
    fi

    if [ -z "$input_version" ]; then
        OPENSSL_VERSION="$DEFAULT_OPENSSL_VERSION"
        OPENSSL_RELEASE="$DEFAULT_OPENSSL_RELEASE"
        OPENSSL_EOL="$DEFAULT_OPENSSL_EOL"
        OPENSSL_LTS="1"

        echo
        echo "Selected OpenSSL $OPENSSL_VERSION [LTS], released: $OPENSSL_RELEASE, support until: $OPENSSL_EOL"
        return
    fi

    if ! [[ "$input_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        echo
        echo "ERROR: Invalid OpenSSL version format: '$input_version'"
        echo "Expected format: X.Y.Z, for example $DEFAULT_OPENSSL_VERSION"
        exit 1
    fi

    while IFS='|' read -r series version release eol lts; do
        if [ "$version" = "$input_version" ]; then
            OPENSSL_VERSION="$version"
            OPENSSL_RELEASE="$release"
            OPENSSL_EOL="$eol"
            OPENSSL_LTS="$lts"

            local tag=""
            if [ "$OPENSSL_LTS" = "1" ]; then
                tag=" [LTS]"
            fi

            echo
            echo "Selected OpenSSL $OPENSSL_VERSION$tag, released: $OPENSSL_RELEASE, support until: $OPENSSL_EOL"
            return
        fi
    done <<< "$OPENSSL_ITEMS"

    echo
    echo "ERROR: OpenSSL version $input_version was not found on the official source page."
    echo
    echo "Available versions:"
    while IFS='|' read -r series version release eol lts; do
        local tag=""
        if [ "$lts" = "1" ]; then
            tag=" [LTS]"
        fi
        echo "  $version$tag"
    done <<< "$OPENSSL_ITEMS"
    exit 1
}

download_openssl()
{
    local version="$1"

    local src_dir="$SRC_DIR/openssl-$version"
    local archive="$SRC_DIR/openssl-$version.tar.gz"
    local url="https://www.openssl.org/source/openssl-$version.tar.gz"

    mkdir -p "$SRC_DIR"

    if [ -f "$src_dir/Configure" ]; then
        echo "OpenSSL sources already exist: $src_dir"
        return
    fi

    echo
    echo "Downloading OpenSSL $version..."
    rm -rf "$src_dir" "$archive"

    wget -O "$archive" "$url"

    tar -xzf "$archive" -C "$SRC_DIR"

    if [ ! -f "$src_dir/Configure" ]; then
        echo "ERROR: Cannot find OpenSSL Configure file:"
        echo "$src_dir/Configure"
        exit 1
    fi
}

verify_library()
{
    local lib="$1"

    echo
    echo "Architecture check: $lib"

    if [ "$CURRENT_TARGET" = "arm64" ]; then
        aarch64-linux-gnu-objdump -f "$lib" | grep '^architecture' || true
    else
        objdump -f "$lib" | grep '^architecture' || true
    fi
}

check_zlib_for_current_target()
{
    if [ ! -f "$CURRENT_ZLIB_PREFIX/lib/libz.a" ]; then
        echo "ERROR: zlib not found for $CURRENT_TARGET:"
        echo "$CURRENT_ZLIB_PREFIX/lib/libz.a"
        echo
        echo "Build zlib first:"
        echo "./build_zlib_linux.sh $CURRENT_TARGET"
        exit 1
    fi

    if [ ! -f "$CURRENT_ZLIB_PREFIX/include/zlib.h" ]; then
        echo "ERROR: zlib headers not found for $CURRENT_TARGET:"
        echo "$CURRENT_ZLIB_PREFIX/include/zlib.h"
        exit 1
    fi
}

build_openssl()
{
    local version="$1"

    local openssl_src_dir="$SRC_DIR/openssl-$version"
    local target="$CURRENT_TARGET"
    local prefix="$CURRENT_PREFIX"
    local target_build_dir="$BUILD_DIR/$target"

    check_zlib_for_current_target

    echo
    echo "========================================"
    echo "Building OpenSSL $version for $target"
    echo "Prefix: $prefix"
    echo "zlib: $CURRENT_ZLIB_PREFIX"
    echo "CC: $CC"
    echo "CXX: $CXX"
    echo "Configure target: $OPENSSL_CONFIG_TARGET"
    echo "========================================"

    rm -rf "$target_build_dir"
    mkdir -p "$target_build_dir"
    mkdir -p "$prefix"

    cp -a "$openssl_src_dir"/. "$target_build_dir"/

    cd "$target_build_dir"

    make clean 2>/dev/null || true

    ./Configure "$OPENSSL_CONFIG_TARGET" \
        --prefix="$prefix" \
        --openssldir="$prefix/ssl" \
        no-shared \
        zlib \
        --with-zlib-include="$CURRENT_ZLIB_PREFIX/include" \
        --with-zlib-lib="$CURRENT_ZLIB_PREFIX/lib"

    make -j"$JOBS"
    make install_sw install_ssldirs

    echo
    echo "OpenSSL installed to: $prefix"

    if [ -f "$prefix/lib64/libssl.a" ]; then
        verify_library "$prefix/lib64/libssl.a"
        verify_library "$prefix/lib64/libcrypto.a"
    elif [ -f "$prefix/lib/libssl.a" ]; then
        verify_library "$prefix/lib/libssl.a"
        verify_library "$prefix/lib/libcrypto.a"
    else
        echo "WARNING: libssl.a was not found in lib or lib64"
    fi

    cd "$ROOT_DIR"
}

build_lnx64()
{
    setup_env_lnx64
    check_tools_lnx64
    build_openssl "$OPENSSL_VERSION"
}

build_arm64()
{
    setup_env_arm64
    check_tools_arm64
    build_openssl "$OPENSSL_VERSION"
}

print_summary()
{
    local openssl_tag=""
    if [ "$OPENSSL_LTS" = "1" ]; then
        openssl_tag=" [LTS]"
    fi

    echo
    echo "========================================"
    echo "Done"
    echo
    echo "OpenSSL version: $OPENSSL_VERSION$openssl_tag"
    echo "Released: $OPENSSL_RELEASE"
    echo "Support until: $OPENSSL_EOL"
    echo
    echo "Built targets:"

    case "$TARGET" in
        lnx64)
            echo "  ✓ lnx64 : $OUT_DIR/lnx64"
            ;;
        arm64)
            echo "  ✓ arm64 : $OUT_DIR/arm64"
            ;;
        all)
            echo "  ✓ lnx64 : $OUT_DIR/lnx64"
            echo "  ✓ arm64 : $OUT_DIR/arm64"
            ;;
    esac

    echo
    echo "Environment restored to lnx64"
    echo "========================================"
}

main()
{
    select_target "${1:-}"
    select_openssl_version "${2:-}"

    reset_locale
    check_tools_common
    download_openssl "$OPENSSL_VERSION"

    case "$TARGET" in
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
            echo "ERROR: Internal error: unsupported TARGET='$TARGET'"
            exit 1
            ;;
    esac

    reset_locale
    setup_env_lnx64

    print_summary
}

main "$@"