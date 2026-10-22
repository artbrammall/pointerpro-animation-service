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

OUTPUT=$(printf "Login Alice\ncreate_canvas 100 100 0\ncreate_rectangle 10 10 255 1\nplace_sprite 1 2 5 5\nplacement_top 3\nset_animation_params 3 1 2 3 4\nDisconnect\n" | ./animate_client "$SERVER_PID")

COUNT=$(echo "$OUTPUT" | grep -c "Success")

if [ "$COUNT" -lt 5 ]; then
    echo "Expected canvas, sprite, placement, movement, and animation param successes"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0