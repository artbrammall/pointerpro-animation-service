#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
ALICE_OUT="tests/stress_shared_alice.out"
BOB_OUT="tests/stress_shared_bob.out"
CAROL_OUT="tests/stress_shared_carol.out"
DAVE_OUT="tests/stress_shared_dave.out"

SERVER_PID=""

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true
    rm -f FIFO_* users.txt "$SERVER_LOG" "$ALICE_OUT" "$BOB_OUT" "$CAROL_OUT" "$DAVE_OUT"
}

trap cleanup EXIT

cleanup

cat > users.txt <<USERS
Alice 10
Bob 10
Carol 10
Dave 10
USERS

./animate_server 4 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

{
    echo "Login Alice"
    sleep 0.5
    echo "create_canvas 100 100 0"
    sleep 0.5
    echo "share_canvas 1 Bob"
    sleep 0.5
    echo "share_canvas 1 Carol"
    sleep 0.5
    echo "share_canvas 1 Dave"
    sleep 8
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$ALICE_OUT" 2>&1 &

ALICE_PID=$!

for i in $(seq 1 50); do
    if grep -q "Success 1" "$ALICE_OUT" 2>/dev/null &&
       [ "$(grep -c "Success" "$ALICE_OUT" 2>/dev/null || true)" -ge 2 ]; then
        break
    fi
    sleep 0.2
done

{
    echo "Login Bob"
    sleep 0.5
    echo "share_canvas 1 Alice"
    sleep 0.5
    echo "create_rectangle 10 10 255 1"
    sleep 0.5
    echo "place_sprite 1 2 5 5"
    sleep 0.5
    echo "barrier 1"
    sleep 0.5
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$BOB_OUT" 2>&1 &

BOB_PID=$!

for i in $(seq 1 30); do
    if grep -q "Success" "$BOB_OUT" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

{
    echo "Login Carol"
    sleep 0.5
    echo "share_canvas 1 Alice"
    sleep 0.5
    echo "barrier 1"
    sleep 0.5
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$CAROL_OUT" 2>&1 &

CAROL_PID=$!

{
    echo "Login Dave"
    sleep 0.5
    echo "share_canvas 1 Alice"
    sleep 0.5
    echo "barrier 1"
    sleep 0.5
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$DAVE_OUT" 2>&1 &

DAVE_PID=$!

wait "$BOB_PID" || true
wait "$CAROL_PID" || true
wait "$DAVE_PID" || true
wait "$ALICE_PID" || true

ALICE_OUTPUT="$(cat "$ALICE_OUT")"
BOB_OUTPUT="$(cat "$BOB_OUT")"
CAROL_OUTPUT="$(cat "$CAROL_OUT")"
DAVE_OUTPUT="$(cat "$DAVE_OUT")"

if ! echo "$ALICE_OUTPUT" | grep -q "Success 1"; then
    echo "FAIL: Alice canvas creation failed"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if [ "$(echo "$ALICE_OUTPUT" | grep -c "Success")" -lt 4 ]; then
    echo "FAIL: Alice did not complete all share requests"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! echo "$BOB_OUTPUT" | grep -q "Success"; then
    echo "FAIL: Bob did not complete shared canvas work"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! echo "$BOB_OUTPUT" | grep -q "Success 2"; then
    echo "FAIL: Bob rectangle creation did not return expected handle"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! echo "$CAROL_OUTPUT" | grep -q "Success"; then
    echo "FAIL: Carol share or barrier failed"
    echo "--- Carol output ---"
    cat "$CAROL_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! echo "$DAVE_OUTPUT" | grep -q "Success"; then
    echo "FAIL: Dave share or barrier failed"
    echo "--- Dave output ---"
    cat "$DAVE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

sleep 2

if ls FIFO_* >/dev/null 2>&1; then
    echo "FAIL: FIFO files were left behind"
    ls -l FIFO_*
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0
