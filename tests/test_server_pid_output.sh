#!/bin/bash

set -u

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

make > /dev/null

output="$(timeout 1 ./animate_server 2)"

if ! echo "$output" | grep -Eq '^Server PID: [0-9]+\.$'; then
    echo "Bad server PID output:"
    echo "$output"
    exit 1
fi

exit 0
