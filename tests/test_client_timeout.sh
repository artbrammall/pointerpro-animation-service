#!/bin/bash

set -u

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

make > /dev/null

if timeout 2 ./animate_client 999999 > /dev/null 2>&1; then
    echo "Client connected to fake server PID"
    exit 1
fi

exit 0
