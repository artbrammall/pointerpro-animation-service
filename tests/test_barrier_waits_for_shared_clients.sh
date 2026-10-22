#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

SERVER_LOG="server.log"
A_IN="a.in"
B_IN="b.in"
A_LOG="a.log"
B_LOG="b.log"

CLIENT_A_PID=""
CLIENT_B_PID=""
SERVER_PID=""

cleanup() {
    exec 3>&- 2>/dev/null || true
    exec 4>&- 2>/dev/null || true
    kill "$CLIENT_A_PID" "$CLIENT_B_PID" "$SERVER_PID" 2>/dev/null || true
    rm -f FIFO_* *.log *.in users.txt
}

trap cleanup EXIT

rm -f FIFO_* *.log *.in users.txt

cat > users.txt << 'USERS'
Alice 10
Bob 10
USERS

mkfifo "$A_IN" "$B_IN"

exec 3<> "$A_IN"
exec 4<> "$B_IN"

../animate_server 2 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!

sleep 1

stdbuf -oL -eL ../animate_client "$SERVER_PID" < "$A_IN" > "$A_LOG" 2>&1 &
CLIENT_A_PID=$!

sleep 2

echo "Login Alice" >&3
sleep 2

if ! grep -q "Welcome Alice" "$A_LOG"; then
    echo "FAIL: Alice did not log in"
    echo "--- Alice log ---"
    cat "$A_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

stdbuf -oL -eL ../animate_client "$SERVER_PID" < "$B_IN" > "$B_LOG" 2>&1 &
CLIENT_B_PID=$!

sleep 2

echo "Login Bob" >&4
sleep 2

if ! grep -q "Welcome Bob" "$B_LOG"; then
    echo "FAIL: Bob did not log in"
    echo "--- Alice log ---"
    cat "$A_LOG"
    echo "--- Bob log ---"
    cat "$B_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

echo "create_canvas 100 100 0" >&3
sleep 2

echo "share_canvas 1 Bob" >&3
sleep 2

echo "share_canvas 1 Alice" >&4
sleep 2

if ! grep -q "Success 1" "$A_LOG"; then
    echo "FAIL: Alice did not create canvas 1"
    echo "--- Alice log ---"
    cat "$A_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! grep -q "Success" "$B_LOG"; then
    echo "FAIL: Bob did not accept canvas share"
    echo "--- Bob log ---"
    cat "$B_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

A_LINES_BEFORE=$(wc -l < "$A_LOG")

echo "barrier 1" >&3
sleep 2

A_NEW_OUTPUT=$(tail -n +"$((A_LINES_BEFORE + 1))" "$A_LOG")

if echo "$A_NEW_OUTPUT" | grep -q "Success"; then
    echo "FAIL: barrier released before Bob reached it"
    echo "--- Alice log ---"
    cat "$A_LOG"
    echo "--- Bob log ---"
    cat "$B_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0