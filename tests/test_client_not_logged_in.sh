#!/bin/bash

set -u

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

make > /dev/null

./animate_server 2 > /dev/null 2>&1 &
SERVER_PID=$!

sleep 1

output="$(printf 'create_canvas 10 10 0\nDisconnect\n' \
    | timeout 3 ./animate_client "$SERVER_PID")"

kill "$SERVER_PID" 2>/dev/null
rm -f FIFO_*

if ! echo "$output" | grep -q '^Not logged in$'; then
    echo "Expected Not logged in"
    echo "$output"
    exit 1
fi

exit 0