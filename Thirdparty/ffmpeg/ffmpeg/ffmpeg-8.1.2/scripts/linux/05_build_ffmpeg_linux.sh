#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

BUILD_SCRIPT="$ROOT_DIR/ffmpeg-build-linux"

TARGET=""
BUILD_LNX64_STATUS="not requested"
BUILD_ARM64_STATUS="not requested"
BUILD_LNX64_MESSAGE=""
BUILD_ARM64_MESSAGE=""

reset_locale()
{
    export LANG=C
    export LC_ALL=C
    export LC_CTYPE=C
    export LC_MESSAGES=C
}

select_target()
{
    local input_target="$1"

    if [ -z "$input_target" ]; then
        echo
        echo "Select FFmpeg build target:"
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

clear_build_env()
{
    unset CC
    unset CXX
    unset AR
    unset RANLIB
    unset LD

    unset CMAKE_C_COMPILER
    unset CMAKE_CXX_COMPILER

    unset PKG_CONFIG_PATH
    unset FFMPEG_EXTRA_CFLAGS
    unset FFMPEG_EXTRA_LDFLAGS
}

setup_env_lnx64()
{
    clear_build_env
    reset_locale

    export CC=/usr/bin/gcc
    export CXX=/usr/bin/g++

    export CMAKE_C_COMPILER=/usr/bin/gcc
    export CMAKE_CXX_COMPILER=/usr/bin/g++

    export PKG_CONFIG_PATH="$ROOT_DIR/zlib/lnx64/lib/pkgconfig:$ROOT_DIR/openssl/lnx64/lib64/pkgconfig:$ROOT_DIR/aom/lnx64/lib/pkgconfig"

    export FFMPEG_EXTRA_CFLAGS="--extra-cflags=-I$ROOT_DIR/zlib/lnx64/include,-I$ROOT_DIR/aom/lnx64/include,-I$ROOT_DIR/openssl/lnx64/include"
    export FFMPEG_EXTRA_LDFLAGS="--extra-ldflags=-Wl,-rpath=\$ORIGIN,-L$ROOT_DIR/zlib/lnx64/lib,-L$ROOT_DIR/aom/lnx64/lib,-L$ROOT_DIR/openssl/lnx64/lib64"
}

setup_env_arm64()
{
    clear_build_env
    reset_locale

    export CC=/usr/bin/aarch64-linux-gnu-gcc
    export CXX=/usr/bin/aarch64-linux-gnu-g++

    export AR=/usr/bin/aarch64-linux-gnu-ar
    export RANLIB=/usr/bin/aarch64-linux-gnu-ranlib
    export LD=/usr/bin/aarch64-linux-gnu-ld

    export CMAKE_C_COMPILER=/usr/bin/aarch64-linux-gnu-gcc
    export CMAKE_CXX_COMPILER=/usr/bin/aarch64-linux-gnu-g++

    export PKG_CONFIG_PATH="$ROOT_DIR/zlib/arm64/lib/pkgconfig:$ROOT_DIR/openssl/arm64/lib/pkgconfig:$ROOT_DIR/aom/arm64/lib/pkgconfig"

    export FFMPEG_EXTRA_CFLAGS="--extra-cflags=-I$ROOT_DIR/zlib/arm64/include,-I$ROOT_DIR/aom/arm64/include,-I$ROOT_DIR/openssl/arm64/include"
    export FFMPEG_EXTRA_LDFLAGS="--extra-ldflags=-Wl,-rpath=\$ORIGIN,-L$ROOT_DIR/zlib/arm64/lib,-L$ROOT_DIR/aom/arm64/lib,-L$ROOT_DIR/openssl/arm64/lib"
}

expected_ffmpeg_bin()
{
    local target="$1"
    local bitness=""

    case "$target" in
        lnx64)
            bitness="linux64"
            ;;
        arm64)
            bitness="linuxARM64"
            ;;
        *)
            return 1
            ;;
    esac

    echo "$ROOT_DIR/redist/ffmpeg-linux-$bitness/release/bin/ffmpeg"
}

verify_target_output()
{
    local target="$1"
    local bin_path

    bin_path="$(expected_ffmpeg_bin "$target")"

    if [ ! -f "$bin_path" ]; then
        echo "ERROR: expected output was not found: $bin_path"
        return 1
    fi

    if [ ! -s "$bin_path" ]; then
        echo "ERROR: expected output is empty: $bin_path"
        return 1
    fi

    return 0
}

apply_runpath_to_target()
{
    local target="$1"
    local bin_dir

    case "$target" in
        lnx64)
            bin_dir="$ROOT_DIR/redist/ffmpeg-linux-linux64/release/bin"
            ;;
        arm64)
            bin_dir="$ROOT_DIR/redist/ffmpeg-linux-linuxARM64/release/bin"
            ;;
        *)
            return 1
            ;;
    esac

    if ! command -v patchelf >/dev/null; then
        echo "WARNING: patchelf not found. RUNPATH was not changed."
        return 0
    fi

    echo
    echo "Applying RUNPATH for $target:"
    echo "Directory: $bin_dir"

    find "$bin_dir" -type f | while read -r file; do
        if file "$file" | grep -q "ELF"; then
            echo "  patchelf --set-rpath '\$ORIGIN' $(basename "$file")"
            patchelf --set-rpath '$ORIGIN' "$file"
        fi
    done
}

build_lnx64()
{
    echo
    echo "========================================"
    echo "Building FFmpeg for lnx64"
    echo "========================================"

    setup_env_lnx64

    if "$BUILD_SCRIPT" release 64 linux && verify_target_output lnx64; then
        apply_runpath_to_target lnx64
        BUILD_LNX64_STATUS="success"
        BUILD_LNX64_MESSAGE="$(expected_ffmpeg_bin lnx64)"
    else
        BUILD_LNX64_STATUS="failed"
        BUILD_LNX64_MESSAGE="See log above for lnx64 failure details"
    fi
}

build_arm64()
{
    echo
    echo "========================================"
    echo "Building FFmpeg for arm64"
    echo "========================================"

    setup_env_arm64

    if "$BUILD_SCRIPT" release arm64 linux && verify_target_output arm64; then
        apply_runpath_to_target arm64
        BUILD_ARM64_STATUS="success"
        BUILD_ARM64_MESSAGE="$(expected_ffmpeg_bin arm64)"
    else
        BUILD_ARM64_STATUS="failed"
        BUILD_ARM64_MESSAGE="See log above for arm64 failure details"
    fi
}

print_target_result()
{
    local target="$1"
    local status="$2"
    local message="$3"

    case "$status" in
        success)
            echo "  ✓ $target : built successfully"
            echo "        $message"
            ;;
        failed)
            echo "  ✗ $target : FAILED"
            echo "        $message"
            ;;
        *)
            echo "  - $target : not requested"
            ;;
    esac
}

print_summary()
{
    echo
    echo "========================================"
    echo "Done"
    echo "Requested target: $TARGET"
    echo
    echo "Build results:"

    case "$TARGET" in
        lnx64)
            print_target_result "lnx64" "$BUILD_LNX64_STATUS" "$BUILD_LNX64_MESSAGE"
            ;;
        arm64)
            print_target_result "arm64" "$BUILD_ARM64_STATUS" "$BUILD_ARM64_MESSAGE"
            ;;
        all)
            print_target_result "lnx64" "$BUILD_LNX64_STATUS" "$BUILD_LNX64_MESSAGE"
            print_target_result "arm64" "$BUILD_ARM64_STATUS" "$BUILD_ARM64_MESSAGE"
            ;;
    esac

    echo
    echo "Environment restored to lnx64"
    echo "========================================"
}

has_failed_builds()
{
    case "$TARGET" in
        lnx64)
            [ "$BUILD_LNX64_STATUS" = "failed" ]
            ;;
        arm64)
            [ "$BUILD_ARM64_STATUS" = "failed" ]
            ;;
        all)
            [ "$BUILD_LNX64_STATUS" = "failed" ] || [ "$BUILD_ARM64_STATUS" = "failed" ]
            ;;
        *)
            return 1
            ;;
    esac
}

main()
{
    if [ ! -x "$BUILD_SCRIPT" ]; then
        echo "ERROR: build script not found or not executable:"
        echo "$BUILD_SCRIPT"
        echo
        echo "Run:"
        echo "chmod +x ffmpeg-build-linux"
        exit 1
    fi

    select_target "${1:-}"

    cd "$ROOT_DIR"
    
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
    esac

    clear_build_env
    reset_locale
    setup_env_lnx64

    print_summary

    if has_failed_builds; then
        exit 1
    fi
}

main "$@"
