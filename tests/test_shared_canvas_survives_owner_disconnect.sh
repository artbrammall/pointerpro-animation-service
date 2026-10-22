#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
A_IN="tests/a_owner_kill.in"
B_IN="tests/b_owner_kill.in"
A_LOG="tests/a_owner_kill.log"
B_LOG="tests/b_owner_kill.log"

SERVER_PID=""
CLIENT_A_PID=""
CLIENT_B_PID=""

cleanup() {
    exec 3>&- 2>/dev/null || true
    exec 4>&- 2>/dev/null || true

    if [ -n "$CLIENT_A_PID" ]; then
        kill "$CLIENT_A_PID" 2>/dev/null || true
        wait "$CLIENT_A_PID" 2>/dev/null || true
    fi

    if [ -n "$CLIENT_B_PID" ]; then
        kill "$CLIENT_B_PID" 2>/dev/null || true
        wait "$CLIENT_B_PID" 2>/dev/null || true
    fi

    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    rm -f FIFO_* users.txt "$SERVER_LOG" "$A_IN" "$B_IN" "$A_LOG" "$B_LOG"
}

trap cleanup EXIT

cleanup

cat > users.txt <<USERS
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

sleep 1

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

echo "create_canvas 64 64 0" >&3
sleep 2

echo "share_canvas 1 Bob" >&3
sleep 2

if ! grep -q "Success 1" "$A_LOG"; then
    echo "FAIL: Alice did not create canvas"
    echo "--- Alice log ---"
    cat "$A_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if [ "$(grep -c "Success" "$A_LOG" 2>/dev/null || true)" -lt 2 ]; then
    echo "FAIL: Alice did not share canvas with Bob"
    echo "--- Alice log ---"
    cat "$A_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

stdbuf -oL -eL ./animate_client "$SERVER_PID" < "$B_IN" > "$B_LOG" 2>&1 &
CLIENT_B_PID=$!

sleep 1

echo "Login Bob" >&4
sleep 2

if ! grep -q "Welcome Bob" "$B_LOG"; then
    echo "FAIL: Bob did not log in"
    echo "--- Bob log ---"
    cat "$B_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

echo "share_canvas 1 Alice" >&4
sleep 2

if ! grep -q "Success" "$B_LOG"; then
    echo "FAIL: Bob did not accept shared canvas"
    echo "--- Alice log ---"
    cat "$A_LOG"
    echo "--- Bob log ---"
    cat "$B_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

kill -9 "$CLIENT_A_PID" 2>/dev/null || true
wait "$CLIENT_A_PID" 2>/dev/null || true
CLIENT_A_PID=""

sleep 4

B_LINES_BEFORE=$(wc -l < "$B_LOG")

echo "create_rectangle 10 10 255 1" >&4
sleep 2

echo "place_sprite 1 2 0 0" >&4
sleep 2

B_NEW_OUTPUT=$(tail -n +"$((B_LINES_BEFORE + 1))" "$B_LOG")

if echo "$B_NEW_OUTPUT" | grep -q "Value error"; then
    echo "FAIL: shared canvas became invalid after owner was killed"
    echo "--- Bob new output ---"
    echo "$B_NEW_OUTPUT"
    echo "--- Full Alice log ---"
    cat "$A_LOG"
    echo "--- Full Bob log ---"
    cat "$B_LOG"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if echo "$B_NEW_OUTPUT" | grep -q "Success 2" &&
   echo "$B_NEW_OUTPUT" | grep -q "Success 3"; then
    exit 0
fi

echo "FAIL: Bob could not use shared canvas after Alice was killed"
echo "--- Bob new output ---"
echo "$B_NEW_OUTPUT"
echo "--- Full Alice log ---"
cat "$A_LOG"
echo "--- Full Bob log ---"
cat "$B_LOG"
echo "--- Server log ---"
cat "$SERVER_LOG"
exit 1
