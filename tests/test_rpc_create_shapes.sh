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

OUTPUT=$(printf "Login Alice\ncreate_rectangle 10 20 255 1\ncreate_circle 5 16711680 1\nDisconnect\n" | ./animate_client "$SERVER_PID")

COUNT=$(echo "$OUTPUT" | grep -c "Success [1-9]")

if [ "$COUNT" -lt 2 ]; then
    echo "Expected rectangle and circle creation to return handles"
    echo "$OUTPUT"
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0