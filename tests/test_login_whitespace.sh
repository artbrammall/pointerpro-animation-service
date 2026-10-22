#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt

cat > users.txt <<EOF
     Alice        10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

OUTPUT=$(printf "Login Alice\nDisconnect\n" | ./animate_client "$SERVER_PID")

echo "$OUTPUT" | grep -q "Welcome Alice. Your balance is 10"

RESULT=$?

kill "$SERVER_PID" 2>/dev/null

rm -f "$SERVER_OUTPUT"
rm -f users.txt
rm -f FIFO_*

exit "$RESULT"