#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt

cat > users.txt <<EOF
Alice 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 4 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

OUTPUT=$(printf "Login Alice\ncreate_canvas 50 50 0\ncreate_rectangle 5 5 255 1\nplace_sprite 1 2 0 0\ncreate_canvas 60 60 0\ncreate_circle 4 255 1\nplace_sprite 4 5 2 2\nplacement_top 3\nplacement_bottom 6\nDisconnect\n" | timeout 8s ./animate_client "$SERVER_PID")

COUNT=$(echo "$OUTPUT" | grep -c "Success")

if [ "$COUNT" -lt 6 ]; then
    echo "Expected operations on different canvases to succeed"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0