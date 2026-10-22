#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"

SERVER_PID=""

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true
    rm -f FIFO_* users.txt "$SERVER_LOG"
}

trap cleanup EXIT

cleanup

cat > users.txt <<USERS
Alice 10
Bob 10
USERS

./animate_server 4 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

OUTPUT=$(
{
    echo "Login Alice"
    sleep 0.2
    echo "create_canvas 100 100 0"
    sleep 0.2
    echo "share_canvas 1 FakeGuy"
    sleep 0.2
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" 2>&1
)

if ! echo "$OUTPUT" | grep -q "Value error"; then
    echo "FAIL: expected Value error for unknown user"
    echo "--- Client output ---"
    echo "$OUTPUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0