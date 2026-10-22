#!/bin/bash

set -u

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

make > /dev/null
rm -f FIFO_*

./animate_server 2 > server_output.txt 2>&1 &
SERVER_PID=$!

sleep 1

if ! printf 'Disconnect\n' | timeout 3 ./animate_client "$SERVER_PID" > /dev/null 2>&1; then
    kill "$SERVER_PID" 2>/dev/null
    rm -f server_output.txt FIFO_*
    echo "Client failed to handshake with server"
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f server_output.txt FIFO_*

exit 0
