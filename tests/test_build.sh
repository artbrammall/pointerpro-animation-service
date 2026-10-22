#!/bin/bash

set -u

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

BUILD_LOG="tests/build.log"

make clean > "$BUILD_LOG" 2>&1

if ! make >> "$BUILD_LOG" 2>&1; then
    echo "FAIL: build failed"
    cat "$BUILD_LOG"
    rm -f "$BUILD_LOG"
    exit 1
fi

rm -f "$BUILD_LOG"

test -x animate_server
test -x animate_client