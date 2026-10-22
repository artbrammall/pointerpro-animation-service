#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
SERVER_PID=""

CLIENT_PREFIX="tests/stress_many_client"

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    pkill -f animate_client 2>/dev/null || true
    rm -f FIFO_* users.txt "$SERVER_LOG" "${CLIENT_PREFIX}"_*.out
}

trap cleanup EXIT

cleanup

cat > users.txt <<USERS
Alice 10
Bob 10
Carol 10
Dave 10
Eve 10
Frank 10
Grace 10
Heidi 10
USERS

./animate_server 4 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!
sleep 1

USERS_LIST="Alice Bob Carol Dave Eve Frank Grace Heidi"
INDEX=0
PIDS=""

for USER in $USERS_LIST; do
    OUT_FILE="${CLIENT_PREFIX}_${USER}.out"

    {
        echo "Login $USER"
        sleep 0.3
        echo "create_canvas 64 64 0"
        sleep 0.3
        echo "create_rectangle 8 8 255 1"
        sleep 0.3
        echo "create_circle 4 65280 1"
        sleep 0.3
        echo "Disconnect"
    } | ./animate_client "$SERVER_PID" > "$OUT_FILE" 2>&1 &

    PIDS="$PIDS $!"
    INDEX=$((INDEX + 1))

    # Small stagger avoids testing only the signal overwrite edge case.
    sleep 0.1
done

FAIL_WAIT=0

for PID in $PIDS; do
    if ! wait "$PID"; then
        FAIL_WAIT=1
    fi
done

if [ "$FAIL_WAIT" -ne 0 ]; then
    echo "FAIL: one or more clients exited with an error"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    echo "--- Client outputs ---"
    cat "${CLIENT_PREFIX}"_*.out
    exit 1
fi

for USER in $USERS_LIST; do
    OUT_FILE="${CLIENT_PREFIX}_${USER}.out"
    OUTPUT="$(cat "$OUT_FILE")"

    if ! echo "$OUTPUT" | grep -q "Welcome $USER. Your balance is 10"; then
        echo "FAIL: $USER did not log in successfully"
        echo "--- $USER output ---"
        cat "$OUT_FILE"
        echo "--- Server log ---"
        cat "$SERVER_LOG"
        exit 1
    fi

    if [ "$(echo "$OUTPUT" | grep -c "Success")" -lt 3 ]; then
        echo "FAIL: $USER did not complete expected RPCs"
        echo "--- $USER output ---"
        cat "$OUT_FILE"
        echo "--- Server log ---"
        cat "$SERVER_LOG"
        exit 1
    fi
done

sleep 2

if ls FIFO_* >/dev/null 2>&1; then
    echo "FAIL: FIFO files were left behind"
    ls -l FIFO_*
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0
