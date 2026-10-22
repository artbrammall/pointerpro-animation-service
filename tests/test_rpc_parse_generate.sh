#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt

cat > users.txt <<EOF
Alice 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" 2>&1 &
SERVER_PID=$!

sleep 1

OUTPUT=$(printf "Login Alice\ngenerate\ngenerate 1\ngenerate 1 out 0 10\ngenerate abc out 0 10 24\ngenerate 1 out abc 10 24\ngenerate 1 out 0 abc 24\ngenerate 1 out 0 10 abc\ngenerate 1 out 0 10 24 extra\nDisconnect\n" | ./animate_client "$SERVER_PID" 2>&1)

COUNT=$(echo "$OUTPUT" | grep -c "RPC Failed")

if [ "$COUNT" -lt 8 ]; then
    echo "FAIL: malformed generate commands should produce RPC Failed"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Server log ---"
    cat "$SERVER_OUTPUT"
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUTPUT" users.txt FIFO_*
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null || true
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0