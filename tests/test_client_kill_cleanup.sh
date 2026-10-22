#!/bin/bash

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT_DIR" || exit 1

SERVER_LOG="tests/server.log"
CLIENT_IN="tests/client_kill.in"
CLIENT_OUT="tests/client_kill.out"

SERVER_PID=""
CLIENT_PID=""

cleanup() {
    exec 3>&- 2>/dev/null || true

    if [ -n "$CLIENT_PID" ]; then
        kill "$CLIENT_PID" 2>/dev/null || true
        wait "$CLIENT_PID" 2>/dev/null || true
    fi

    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    rm -f FIFO_* users.txt "$SERVER_LOG" "$CLIENT_IN" "$CLIENT_OUT"
}

trap cleanup EXIT

cleanup

cat > users.txt <<USERS
Alice 10
USERS

mkfifo "$CLIENT_IN"

exec 3<> "$CLIENT_IN"

./animate_server 2 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!

sleep 1

stdbuf -oL -eL ./animate_client "$SERVER_PID" < "$CLIENT_IN" > "$CLIENT_OUT" 2>&1 &
CLIENT_PID=$!

sleep 1

echo "Login Alice" >&3
sleep 2

if ! grep -q "Welcome Alice. Your balance is 10" "$CLIENT_OUT"; then
    echo "FAIL: client did not log in"
    echo "--- Client output ---"
    cat "$CLIENT_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

if ! find . -maxdepth 1 -name "FIFO_*" | grep -q .; then
    echo "FAIL: FIFOs did not exist before killing client"
    echo "--- Client output ---"
    cat "$CLIENT_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

kill -9 "$CLIENT_PID" 2>/dev/null || true
wait "$CLIENT_PID" 2>/dev/null || true
CLIENT_PID=""

sleep 3

if find . -maxdepth 1 -name "FIFO_*" | grep -q .; then
    echo "FAIL: FIFOs still exist after killed client"
    echo "--- FIFOs ---"
    find . -maxdepth 1 -name "FIFO_*"
    echo "--- Client output ---"
    cat "$CLIENT_OUT"
    echo "--- Server log ---"
    cat "$SERVER_LOG"
    exit 1
fi

exit 0