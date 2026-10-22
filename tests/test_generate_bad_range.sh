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

OUTPUT=$(printf "Login Alice\ncreate_canvas 64 64 0\ngenerate 1 out -1 10 24\ngenerate 1 out 0 -1 24\ngenerate 1 out 10 0 24\ngenerate 1 out 0 10 0\ngenerate 1 out 0 10 -24\ngenerate 1 out 0 100001 24\nDisconnect\n" | ./animate_client "$SERVER_PID" 2>&1)

COUNT=$(echo "$OUTPUT" | grep -c "Value error")

if [ "$COUNT" -lt 6 ]; then
    echo "FAIL: bad generate ranges should produce Value error"
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