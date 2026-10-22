#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_*

SERVER_OUTPUT=$(mktemp)

./animate_server 1 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

{
    sleep 2
    printf "Disconnect\n"
} | timeout 5s ./animate_client "$SERVER_PID" > /dev/null &
CLIENT_PID=$!

sleep 1

FIFO_COUNT=$(find . -maxdepth 1 -name "FIFO_*" | wc -l)

if [ "$FIFO_COUNT" -ne 2 ]; then
    echo "Expected 2 FIFOs, found $FIFO_COUNT"
    kill "$CLIENT_PID" 2>/dev/null
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" FIFO_*
    exit 1
fi

wait "$CLIENT_PID" 2>/dev/null || true

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" FIFO_*

exit 0