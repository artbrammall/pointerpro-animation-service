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

OUTPUT=$(printf "Login Alice\ncreate_canvas abc 100 0\ncreate_rectangle 10 10 255 2\nplace_sprite 1 2 x y\nDisconnect\n" | ./animate_client "$SERVER_PID")

COUNT=$(echo "$OUTPUT" | grep -c "RPC Failed")

if [ "$COUNT" -lt 3 ]; then
    echo "Expected at least 3 RPC Failed responses for bad args"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0