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
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

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

for i in $(seq 1 50); do
    if grep -q "^Success 1$" "$ALICE_OUT" 2>/dev/null &&
       grep -q "^Success$" "$ALICE_OUT" 2>/dev/null; then
        break
    fi

    sleep 0.2
done

if ! grep -q "^Success 1$" "$ALICE_OUT" 2>/dev/null ||
   ! grep -q "^Success$" "$ALICE_OUT" 2>/dev/null; then
    echo "FAIL: Alice did not create and share canvas"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

{
    echo "Login Bob"
    sleep 0.2
    echo "share_canvas 1 Alice"
    sleep 0.2
    echo "create_rectangle 10 10 255 1"
    sleep 0.2
    echo "place_sprite 1 2 0 0"
    sleep 0.2
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$BOB_OUT" 2>&1

BOB_OUTPUT="$(cat "$BOB_OUT")"

if ! echo "$BOB_OUTPUT" | grep -q "^Welcome Bob\. Your balance is 10$"; then
    echo "FAIL: Bob did not log in"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! echo "$BOB_OUTPUT" | grep -q "^Success$"; then
    echo "FAIL: Bob did not complete share_canvas"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! echo "$BOB_OUTPUT" | grep -q "^Success 2$"; then
    echo "FAIL: Bob did not create sprite"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

NUMBERED_SUCCESSES="$(echo "$BOB_OUTPUT" | grep -Ec "^Success [0-9]+$")"

if [ "$NUMBERED_SUCCESSES" -lt 2 ]; then
    echo "FAIL: Bob could not place sprite on shared canvas"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0