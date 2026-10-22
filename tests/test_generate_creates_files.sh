#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt out.dat out.mp4 out.log

cat > users.txt <<EOF
Alice 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" 2>&1 &
SERVER_PID=$!

sleep 1

OUTPUT=$(
printf "Login Alice\ncreate_canvas 64 64 0\ncreate_rectangle 10 10 16711680 1\nplace_sprite 1 2 5 5\ngenerate 1 out 0 2 24\nDisconnect\n" |
    ./animate_client "$SERVER_PID" 2>&1
)

if [ ! -f out.dat ] || [ ! -f out.mp4 ] || [ ! -f out.log ]; then
    echo "FAIL: generate did not create expected output files"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Files ---"
    ls -l out.dat out.mp4 out.log 2>/dev/null || true
    echo "--- Server log ---"
    cat "$SERVER_OUTPUT"
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* out.dat out.mp4 out.log
    exit 1
fi

if [ ! -s out.dat ]; then
    echo "FAIL: out.dat was empty"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Server log ---"
    cat "$SERVER_OUTPUT"
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* out.dat out.mp4 out.log
    exit 1
fi

if [ ! -s out.log ]; then
    echo "FAIL: out.log was empty"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Server log ---"
    cat "$SERVER_OUTPUT"
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* out.dat out.mp4 out.log
    exit 1
fi

if [ ! -s out.mp4 ]; then
    echo "FAIL: out.mp4 was empty"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Server log ---"
    cat "$SERVER_OUTPUT"
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* out.dat out.mp4 out.log
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null || true
rm -f "$SERVER_OUTPUT" users.txt FIFO_*

exit 0
