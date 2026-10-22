#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
ALICE_OUT="tests/alice.out"

SERVER_PID=""

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true
    rm -f FIFO_* users.txt "$SERVER_LOG" "$ALICE_OUT"
}

trap cleanup EXIT

cleanup

cat > users.txt <<USERS
Alice 10
Bob 10
Mallory 10
USERS

./animate_server 4 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

{
    echo "Login Alice"
    sleep 0.2
    echo "create_canvas 100 100 0"
    sleep 2
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$ALICE_OUT" 2>&1 &

sleep 1

MALLORY_OUTPUT=$(
{
    echo "Login Mallory"
    sleep 0.2
    echo "share_canvas 1 Bob"
    sleep 0.2
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" 2>&1
)

if ! echo "$MALLORY_OUTPUT" | grep -q "Value error"; then
    echo "FAIL: expected Value error for unowned canvas"
    echo "--- Mallory output ---"
    echo "$MALLORY_OUTPUT"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0