#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
A_IN="tests/a.in"
B_IN="tests/b.in"
A_LOG="tests/a.log"
B_LOG="tests/b.log"

CLIENT_A_PID=""
CLIENT_B_PID=""
SERVER_PID=""

cleanup() {
    exec 3>&- 2>/dev/null || true
    exec 4>&- 2>/dev/null || true

    kill "$CLIENT_A_PID" "$CLIENT_B_PID" "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true

    rm -f FIFO_* users.txt "$SERVER_LOG" "$A_IN" "$B_IN" "$A_LOG" "$B_LOG"
}

trap cleanup EXIT

cleanup

cat > users.txt << 'USERS'
Alice 10
Bob 10
USERS

mkfifo "$A_IN" "$B_IN"

exec 3<> "$A_IN"
exec 4<> "$B_IN"

./animate_server 2 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!

sleep 1

stdbuf -oL -eL ./animate_client "$SERVER_PID" < "$A_IN" > "$A_LOG" 2>&1 &
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

stdbuf -oL -eL ./animate_client "$SERVER_PID" < "$B_IN" > "$B_LOG" 2>&1 &
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
B_LINES_BEFORE=$(wc -l < "$B_LOG")

echo "barrier 1" >&3
sleep 2

A_AFTER_ALICE_ONLY=$(tail -n +"$((A_LINES_BEFORE + 1))" "$A_LOG")

if echo "$A_AFTER_ALICE_ONLY" | grep -q "Success"; then
    echo "FAIL: Alice barrier released before Bob arrived"
    echo "--- Alice new output ---"
    echo "$A_AFTER_ALICE_ONLY"
    echo "--- Full Alice log ---"
    cat "$A_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

echo "barrier 1" >&4
sleep 3

A_NEW_OUTPUT=$(tail -n +"$((A_LINES_BEFORE + 1))" "$A_LOG")
B_NEW_OUTPUT=$(tail -n +"$((B_LINES_BEFORE + 1))" "$B_LOG")

if echo "$A_NEW_OUTPUT" | grep -q "Success" &&
   echo "$B_NEW_OUTPUT" | grep -q "Success"; then
    exit 0
fi

echo "FAIL: barrier did not release after all clients arrived"
echo "--- Full Alice log ---"
cat "$A_LOG"
echo "--- Full Bob log ---"
cat "$B_LOG"
echo "--- Server log ---"
cat "$SERVER_LOG"
exit 1