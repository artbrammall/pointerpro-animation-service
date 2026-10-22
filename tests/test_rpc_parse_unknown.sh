#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt

cat > users.txt <<EOF
Alice 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

OUTPUT=$(printf "Login Alice\nnonsense_command\nDisconnect\n" | ./animate_client "$SERVER_PID")

if ! echo "$OUTPUT" | grep -q "RPC Failed"; then
    echo "Expected RPC Failed for unknown command"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0