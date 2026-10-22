#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt out.mp4 out.log
rm -rf out.dat

cat > users.txt <<EOF
Alice 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" 2>&1 &
SERVER_PID=$!

sleep 1

mkdir out.dat

OUTPUT=$(
printf "Login Alice\ncreate_canvas 64 64 0\ngenerate 1 out 0 1 24\nDisconnect\n" |
    ./animate_client "$SERVER_PID" 2>&1
)

if ! echo "$OUTPUT" | grep -q "Data write failed"; then
    echo "FAIL: expected Data write failed when out.dat is a directory"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Files ---"
    ls -ld out.dat out.mp4 out.log 2>/dev/null || true
    echo "--- Server log ---"
    cat "$SERVER_OUTPUT"
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* out.mp4 out.log
    rm -rf out.dat
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null || true
rm -f "$SERVER_OUTPUT" users.txt FIFO_* out.mp4 out.log
rm -rf out.dat

exit 0
