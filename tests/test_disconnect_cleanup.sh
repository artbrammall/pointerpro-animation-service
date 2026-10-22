#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_*

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

{
    sleep 1

    if ! find . -maxdepth 1 -name "FIFO_*" | grep -q .; then
        echo "Expected FIFOs to exist after connection"
        kill "$SERVER_PID" 2>/dev/null
        rm -f "$SERVER_OUTPUT"
        exit 1
    fi

    printf "Disconnect\n"
} | ./animate_client "$SERVER_PID"

sleep 1

if find . -maxdepth 1 -name "FIFO_*" | grep -q .; then
    echo "FIFOs still exist after Disconnect"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT"
    rm -f FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT"

exit 0