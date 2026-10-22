#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_*

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

./animate_client "$SERVER_PID" <<EOF
Disconnect
EOF

sleep 1

FIFO_COUNT=$(find . -maxdepth 1 -name "FIFO_*" | wc -l)

if [ "$FIFO_COUNT" -ne 0 ]; then
    echo "Expected FIFOs to be removed after disconnect"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT"
    rm -f FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT"

exit 0