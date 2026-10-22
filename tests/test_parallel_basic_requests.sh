#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt

cat > users.txt <<EOF
Alice 10
Bob 10
Charlie 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 3 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

printf "Login Alice\ncreate_canvas 20 20 0\ncreate_rectangle 5 5 255 1\nDisconnect\n" \
    | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client_alice.out &
PID1=$!

sleep 0.2

printf "Login Bob\ncreate_canvas 20 20 0\ncreate_circle 5 255 1\nDisconnect\n" \
    | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client_bob.out &
PID2=$!

sleep 0.2

printf "Login Charlie\ncreate_canvas 20 20 0\ncreate_rectangle 3 3 123 1\nDisconnect\n" \
    | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client_charlie.out &
PID3=$!

wait "$PID1" || true
wait "$PID2" || true
wait "$PID3" || true

if ! grep -q "Success" /tmp/client_alice.out; then
    echo "Alice client did not receive Success"
    cat /tmp/client_alice.out
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* /tmp/client_alice.out /tmp/client_bob.out /tmp/client_charlie.out
    exit 1
fi

if ! grep -q "Success" /tmp/client_bob.out; then
    echo "Bob client did not receive Success"
    cat /tmp/client_bob.out
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* /tmp/client_alice.out /tmp/client_bob.out /tmp/client_charlie.out
    exit 1
fi

if ! grep -q "Success" /tmp/client_charlie.out; then
    echo "Charlie client did not receive Success"
    cat /tmp/client_charlie.out
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* /tmp/client_alice.out /tmp/client_bob.out /tmp/client_charlie.out
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_* /tmp/client_alice.out /tmp/client_bob.out /tmp/client_charlie.out

exit 0