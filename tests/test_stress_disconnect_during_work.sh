#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
ALICE_OUT="tests/stress_disconnect_alice.out"
BOB_OUT="tests/stress_disconnect_bob.out"
ALICE_IN="tests/stress_disconnect_alice.in"

SERVER_PID=""
ALICE_PID=""
WRITER_PID=""

cleanup() {
    kill "$ALICE_PID" 2>/dev/null || true
    kill "$WRITER_PID" 2>/dev/null || true
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true
    rm -f FIFO_* users.txt "$SERVER_LOG" "$ALICE_OUT" "$BOB_OUT" "$ALICE_IN" \
        stress_kill.dat stress_kill.mp4 stress_kill.log
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

mkfifo "$ALICE_IN"

./animate_client "$SERVER_PID" < "$ALICE_IN" > "$ALICE_OUT" 2>&1 &
ALICE_PID=$!

{
    echo "Login Alice"
    sleep 0.5
    echo "create_canvas 64 64 0"
    sleep 0.5
    echo "generate 1 stress_kill 0 100 24"
    sleep 10
    echo "Disconnect"
} > "$ALICE_IN" &

WRITER_PID=$!

for i in $(seq 1 40); do
    if grep -q "Success 1" "$ALICE_OUT" 2>/dev/null; then
        break
    fi
    sleep 0.2
done

if ! grep -q "Success 1" "$ALICE_OUT" 2>/dev/null; then
    echo "FAIL: Alice did not create canvas before disconnect test"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

# Give generate time to enter server side work, then kill the client brutally.
sleep 1
kill -9 "$ALICE_PID" 2>/dev/null || true
kill "$WRITER_PID" 2>/dev/null || true

# Reap the killed background jobs so bash does not print noisy "Killed" lines.
wait "$ALICE_PID" 2>/dev/null || true
wait "$WRITER_PID" 2>/dev/null || true

# Allow the server liveness check to notice the dead client and clean up.
sleep 4

{
    echo "Login Bob"
    sleep 0.5
    echo "create_canvas 32 32 0"
    sleep 0.5
    echo "create_rectangle 4 4 255 1"
    sleep 0.5
    echo "Disconnect"
} | ./animate_client "$SERVER_PID" > "$BOB_OUT" 2>&1

BOB_OUTPUT="$(cat "$BOB_OUT")"

if ! echo "$BOB_OUTPUT" | grep -q "Welcome Bob. Your balance is 10"; then
    echo "FAIL: server did not accept Bob after Alice was killed"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if [ "$(echo "$BOB_OUTPUT" | grep -c "Success")" -lt 2 ]; then
    echo "FAIL: server did not keep processing requests after killed client"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

sleep 2

if ls FIFO_* >/dev/null 2>&1; then
    echo "FAIL: FIFO files were left behind after killed client cleanup"
    ls -l FIFO_*
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0
