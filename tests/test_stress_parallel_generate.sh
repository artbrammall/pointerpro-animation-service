#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
ALICE_OUT="tests/stress_generate_alice.out"
BOB_OUT="tests/stress_generate_bob.out"

SERVER_PID=""

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true
    rm -f FIFO_* users.txt "$SERVER_LOG" "$ALICE_OUT" "$BOB_OUT" \
        stress_alice.dat stress_alice.mp4 stress_alice.log \
        stress_bob.dat stress_bob.mp4 stress_bob.log
}

trap cleanup EXIT

cleanup

cat > users.txt <<USERS
Alice 10
Bob 10
USERS

./animate_server 2 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

timeout 20s bash -c "
{
    echo 'Login Alice'
    sleep 0.3
    echo 'create_canvas 32 32 0'
    sleep 0.3
    echo 'create_rectangle 4 4 255 1'
    sleep 0.3
    echo 'place_sprite 1 2 2 2'
    sleep 0.3
    echo 'generate 1 stress_alice 0 1 12'
    sleep 0.3
    echo 'Disconnect'
} | ./animate_client '$SERVER_PID'
" > "$ALICE_OUT" 2>&1

if [ "$?" -ne 0 ]; then
    echo "FAIL: Alice generate client timed out or failed"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

timeout 20s bash -c "
{
    echo 'Login Bob'
    sleep 0.3
    echo 'create_canvas 32 32 0'
    sleep 0.3
    echo 'create_circle 3 65280 1'
    sleep 0.3
    echo 'place_sprite 4 5 5 5'
    sleep 0.3
    echo 'generate 4 stress_bob 0 1 12'
    sleep 0.3
    echo 'Disconnect'
} | ./animate_client '$SERVER_PID'
" > "$BOB_OUT" 2>&1

if [ "$?" -ne 0 ]; then
    echo "FAIL: Bob generate client timed out or failed"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if grep -q "RPC Failed\|Value error\|Internal error\|Data write failed\|Movie write failed" "$ALICE_OUT"; then
    echo "FAIL: Alice generate returned an error"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if grep -q "RPC Failed\|Value error\|Internal error\|Data write failed\|Movie write failed" "$BOB_OUT"; then
    echo "FAIL: Bob generate returned an error"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! grep -q "Success" "$ALICE_OUT"; then
    echo "FAIL: Alice had no success output"
    echo "--- Alice output ---"
    cat "$ALICE_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! grep -q "Success" "$BOB_OUT"; then
    echo "FAIL: Bob had no success output"
    echo "--- Bob output ---"
    cat "$BOB_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

for NAME in stress_alice stress_bob; do
    if [ ! -s "$NAME.dat" ]; then
        echo "FAIL: missing or empty $NAME.dat"
        echo "--- Server log ---"
        cat "$SERVER_LOG"
        exit 1
    fi

    if [ ! -s "$NAME.mp4" ]; then
        echo "FAIL: missing or empty $NAME.mp4"
        echo "--- Server log ---"
        cat "$SERVER_LOG"
        exit 1
    fi

    if [ ! -f "$NAME.log" ]; then
        echo "FAIL: missing $NAME.log"
        echo "--- Server log ---"
        cat "$SERVER_LOG"
        exit 1
    fi
done

sleep 1

if ls FIFO_* >/dev/null 2>&1; then
    echo "FAIL: FIFO files were left behind"
    ls -l FIFO_*
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0