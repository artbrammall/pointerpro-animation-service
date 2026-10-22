#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
ALICE_OUT="tests/alice.out"
BOB_OUT="tests/bob.out"

SERVER_PID=""

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true
    rm -f FIFO_* users.txt "$SERVER_LOG" "$ALICE_OUT" "$BOB_OUT"
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

{
    echo "Login Alice"
    sleep 0.2
    echo "create_canvas 100 100 0"
    sleep 0.2
    echo "share_canvas 1 Bob"
    sleep 8
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$ALICE_OUT" 2>&1 &

for i in $(seq 1 40); do
    if grep -q "Success 1" "$ALICE_OUT" 2>/dev/null &&
       [ "$(grep -c "Success" "$ALICE_OUT" 2>/dev/null || true)" -ge 2 ]; then
        break
    fi
    sleep 0.2
done

{
    echo "Login Bob"
    sleep 0.2
    echo "share_canvas 1 Alice"
    sleep 0.2
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$BOB_OUT" 2>&1

sleep 1

ALICE_OUTPUT="$(cat "$ALICE_OUT")"
BOB_OUTPUT="$(cat "$BOB_OUT")"

if ! echo "$ALICE_OUTPUT" | grep -q "Success 1"; then
    echo "FAIL: canvas creation failed"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if [ "$(echo "$ALICE_OUTPUT" | grep -c "Success")" -lt 2 ]; then
    echo "FAIL: Alice share failed"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! echo "$BOB_OUTPUT" | grep -q "Success"; then
    echo "FAIL: Bob share failed"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0