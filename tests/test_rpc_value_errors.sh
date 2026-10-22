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

OUTPUT=$(printf "Login Alice\ndestroy_canvas 0\ndestroy_sprite 999\nplace_sprite 999 888 0 0\nplacement_top 999\nDisconnect\n" | ./animate_client "$SERVER_PID")

COUNT=$(echo "$OUTPUT" | grep -c "Value error")

if [ "$COUNT" -lt 4 ]; then
    echo "Expected invalid handles to produce Value error"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0