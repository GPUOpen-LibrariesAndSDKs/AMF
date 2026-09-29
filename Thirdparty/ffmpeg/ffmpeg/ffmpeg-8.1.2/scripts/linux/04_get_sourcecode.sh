#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [ ! -x "$ROOT_DIR/get_sourcecode" ]; then
    echo "ERROR: get_sourcecode not found:"
    echo "  $ROOT_DIR/get_sourcecode"
    exit 1
fi

cd "$ROOT_DIR"

exec ./get_sourcecode "$@"