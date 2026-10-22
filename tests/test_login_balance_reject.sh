#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt

cat > users.txt <<EOF
Alice 10
Bob 0
Charlie -5
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

OUTPUT1=$(printf "Login Bob\n" | ./animate_client "$SERVER_PID")
OUTPUT2=$(printf "Login Charlie\n" | ./animate_client "$SERVER_PID")

echo "$OUTPUT1" | grep -q "Reject BALANCE"
RESULT1=$?

echo "$OUTPUT2" | grep -q "Reject BALANCE"
RESULT2=$?

kill "$SERVER_PID" 2>/dev/null

rm -f "$SERVER_OUTPUT"
rm -f users.txt
rm -f FIFO_*

if [ "$RESULT1" -ne 0 ] || [ "$RESULT2" -ne 0 ]; then
    exit 1
fi

exit 0