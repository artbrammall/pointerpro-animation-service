#!/bin/bash

set -u

SERVER_LOG="server.log"
CLIENT_A_LOG="client_a.log"
CLIENT_B_LOG="client_b.log"

SERVER_PID=""

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    rm -f FIFO_* *.log *.in users.txt
}

trap cleanup EXIT

rm -f FIFO_* *.log *.in users.txt

cat > users.txt << 'USERS'
Alice 10
Mallory 10
USERS

../animate_server 2 > "$SERVER_LOG" 2>&1 &
SERVER_PID=$!

sleep 1

{
    echo "Login Alice"
    echo "create_canvas 100 100 0"
    sleep 1
} | ../animate_client "$SERVER_PID" > "$CLIENT_A_LOG" 2>&1 &

sleep 2

{
    echo "Login Mallory"
    echo "barrier 1"
    sleep 1
} | ../animate_client "$SERVER_PID" > "$CLIENT_B_LOG" 2>&1

if grep -q "Value error" "$CLIENT_B_LOG"; then
    exit 0
fi

echo "FAIL: barrier did not reject unshared canvas"
echo "--- Mallory log ---"
cat "$CLIENT_B_LOG"
echo "--- Server log ---"
cat "$SERVER_LOG"
exit 1