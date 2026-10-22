#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt

cat > users.txt <<EOF
Alice 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 1 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

OUTPUT=$(printf "Login Alice\ncreate_canvas 100 100 0\ncreate_rectangle 10 10 255 1\nplace_sprite 1 2 5 5\ndestroy_placement 3\ndestroy_sprite 2\ndestroy_canvas 1\nDisconnect\n" | ./animate_client "$SERVER_PID")

if ! echo "$OUTPUT" | grep -q "Success 0"; then
    echo "Expected destroy_sprite to return Success 0"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

COUNT=$(echo "$OUTPUT" | grep -c "Success")

if [ "$COUNT" -lt 6 ]; then
    echo "Expected destroy commands to succeed"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0