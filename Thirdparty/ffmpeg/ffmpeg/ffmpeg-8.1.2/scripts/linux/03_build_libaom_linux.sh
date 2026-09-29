#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

SRC_DIR="$ROOT_DIR/sources"
AOM_SRC_DIR="$SRC_DIR/aom"
BUILD_DIR="$ROOT_DIR/build/aom"
OUT_DIR="$ROOT_DIR/aom"

AOM_GIT_URL="https://aomedia.googlesource.com/aom"

TARGET=""
JOBS="$(nproc)"
BUILD_START_TIME=0

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
    export CURRENT_BUILD_DIR="$BUILD_DIR/lnx64"

    export AOM_CMAKE_FLAGS=(
        -G "Unix Makefiles"
        -DCMAKE_INSTALL_PREFIX="$CURRENT_PREFIX"
        -DBUILD_SHARED_LIBS=0
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_FLAGS="-flto -O3 -march=znver2"
        -DCMAKE_C_FLAGS="-flto -O3 -march=znver2"
        -DCMAKE_C_FLAGS_INIT="-flto=8 -static"
        -DENABLE_NASM=on
    )
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
    export CURRENT_BUILD_DIR="$BUILD_DIR/arm64"

    export AOM_CMAKE_FLAGS=(
        -G "Unix Makefiles"
        -DCMAKE_INSTALL_PREFIX="$CURRENT_PREFIX"
        -DBUILD_SHARED_LIBS=0
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_CXX_FLAGS="-flto -O3 -march=armv8-a"
        -DCMAKE_C_FLAGS="-flto -O3 -march=armv8-a"
        -DAOM_TARGET_CPU=arm64
        -DENABLE_NASM=off
        -DCMAKE_TOOLCHAIN_FILE="$AOM_SRC_DIR/cmake/toolchains/arm64-linux-gcc.cmake"
    )
}

check_tools_common()
{
    command -v git >/dev/null || { echo "ERROR: git not found"; exit 1; }
    command -v cmake >/dev/null || { echo "ERROR: cmake not found"; exit 1; }
    command -v make >/dev/null || { echo "ERROR: make not found"; exit 1; }
    command -v objdump >/dev/null || { echo "ERROR: objdump not found"; exit 1; }
}

check_tools_lnx64()
{
    command -v gcc >/dev/null || { echo "ERROR: gcc not found"; exit 1; }
    command -v g++ >/dev/null || { echo "ERROR: g++ not found"; exit 1; }
    command -v nasm >/dev/null || { echo "ERROR: nasm not found"; exit 1; }
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

clone_or_update_aom()
{
    mkdir -p "$SRC_DIR"

    if [ -d "$AOM_SRC_DIR/.git" ]; then
        echo
        echo "AOM sources already exist: $AOM_SRC_DIR"
        echo "Updating AOM sources..."
        git -C "$AOM_SRC_DIR" fetch --depth 1 origin
        git -C "$AOM_SRC_DIR" reset --hard origin/main
        return
    fi

    echo
    echo "Cloning AOM sources..."
    git clone --depth 1 "$AOM_GIT_URL" "$AOM_SRC_DIR"
}

print_aom_version()
{
    echo
    echo "AOM source revision:"
    git -C "$AOM_SRC_DIR" log -1 --pretty=format:"  commit: %H%n  date:   %cd%n  title:  %s" --date=short
    echo
}

print_tool_versions()
{
    echo
    echo "Tool versions:"
    printf "  %-8s %s\n" "CC:" "$($CC --version | head -n1)"
    printf "  %-8s %s\n" "CXX:" "$($CXX --version | head -n1)"
    printf "  %-8s %s\n" "cmake:" "$(cmake --version | head -n1)"
    printf "  %-8s %s\n" "make:" "$(make --version | head -n1)"

    if command -v nasm >/dev/null; then
        printf "  %-8s %s\n" "nasm:" "$(nasm -v)"
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

build_aom()
{
    local target="$CURRENT_TARGET"
    local prefix="$CURRENT_PREFIX"
    local build_dir="$CURRENT_BUILD_DIR"

    echo
    echo "========================================"
    echo "Building AOM for $target"
    echo "Prefix: $prefix"
    echo "CC: $CC"
    echo "CXX: $CXX"
    print_tool_versions
    echo "========================================"

    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    mkdir -p "$prefix"

    cd "$build_dir"

    cmake "${AOM_CMAKE_FLAGS[@]}" "$AOM_SRC_DIR"

    make -j"$JOBS"
    make install

    echo
    echo "AOM installed to: $prefix"

    if [ -f "$prefix/lib/libaom.a" ]; then
        verify_library "$prefix/lib/libaom.a"
    elif [ -f "$prefix/lib64/libaom.a" ]; then
        verify_library "$prefix/lib64/libaom.a"
    else
        echo "WARNING: libaom.a was not found in lib or lib64"
    fi

    cd "$ROOT_DIR"
}

build_lnx64()
{
    setup_env_lnx64
    check_tools_lnx64
    build_aom
}

build_arm64()
{
    setup_env_arm64
    check_tools_arm64
    build_aom
}

format_elapsed_time()
{
    local elapsed="$1"

    printf "%02d:%02d:%02d" \
        $((elapsed / 3600)) \
        $(((elapsed % 3600) / 60)) \
        $((elapsed % 60))
}

print_summary()
{
    local end_time elapsed elapsed_formatted

    end_time="$(date +%s)"
    elapsed=$((end_time - BUILD_START_TIME))
    elapsed_formatted="$(format_elapsed_time "$elapsed")"

    echo
    echo "========================================"
    echo "Done"
    echo
    print_aom_version
    echo "Elapsed time: $elapsed_formatted"
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
    BUILD_START_TIME="$(date +%s)"

    select_target "${1:-}"

    reset_locale
    check_tools_common
    clone_or_update_aom
    print_aom_version

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