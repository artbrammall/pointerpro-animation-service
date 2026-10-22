#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt evil.dat evil.mp4 evil.log

cat > users.txt <<EOF
Alice 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" 2>&1 &
SERVER_PID=$!

sleep 1

LONG_NAME="$(printf 'a%.0s' $(seq 1 129))"

OUTPUT=$(
printf "Login Alice\ncreate_canvas 64 64 0\ngenerate 1 ../evil 0 10 24\ngenerate 1 ../../evil 0 10 24\ngenerate 1 bad/name 0 10 24\ngenerate 1 bad\\\\name 0 10 24\ngenerate 1 out..bad 0 10 24\ngenerate 1 -bad 0 10 24\ngenerate 1 bad\$name 0 10 24\ngenerate 1 %s 0 10 24\nDisconnect\n" "$LONG_NAME" | ./animate_client "$SERVER_PID" 2>&1
)

COUNT=$(echo "$OUTPUT" | grep -c "Value error")

if [ "$COUNT" -lt 8 ]; then
    echo "FAIL: bad generate filenames should produce Value error"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Server log ---"
    cat "$SERVER_OUTPUT"
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* evil.dat evil.mp4 evil.log
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null || true
rm -f "$SERVER_OUTPUT" users.txt FIFO_* evil.dat evil.mp4 evil.log

exit 0
