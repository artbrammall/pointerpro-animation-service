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

OUTPUT=$(printf "Login Alice\ncreate_canvas 80 80 0\ncreate_rectangle 5 5 255 1\ncreate_circle 4 123 1\nplace_sprite 1 2 0 0\nplace_sprite 1 3 10 10\nplacement_top 4\nplacement_bottom 5\nset_animation_params 4 1 2 3 4\nset_animation_params 5 4 3 2 1\nDisconnect\n" | timeout 8s ./animate_client "$SERVER_PID")

COUNT=$(echo "$OUTPUT" | grep -c "Success")

if [ "$COUNT" -lt 6 ]; then
    echo "Expected same canvas mutations to succeed without races"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0