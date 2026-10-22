#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt \
    /tmp/client1.out /tmp/client2.out /tmp/client3.out /tmp/client4.out \
    /tmp/client1.err /tmp/client2.err /tmp/client3.err /tmp/client4.err

cat > users.txt <<EOF
Alice 10
Bob 10
Charlie 10
Dave 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 2 > "$SERVER_OUTPUT" 2>&1 &
SERVER_PID=$!

sleep 1

printf "Login Alice\ncreate_canvas 10 10 0\nDisconnect\n" | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client1.out 2>/tmp/client1.err &
PID1=$!
sleep 0.1

printf "Login Bob\ncreate_canvas 10 10 0\nDisconnect\n" | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client2.out 2>/tmp/client2.err &
PID2=$!
sleep 0.1

printf "Login Charlie\ncreate_canvas 10 10 0\nDisconnect\n" | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client3.out 2>/tmp/client3.err &
PID3=$!
sleep 0.1

printf "Login Dave\ncreate_canvas 10 10 0\nDisconnect\n" | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client4.out 2>/tmp/client4.err &
PID4=$!

wait "$PID1"
STATUS1=$?
wait "$PID2"
STATUS2=$?
wait "$PID3"
STATUS3=$?
wait "$PID4"
STATUS4=$?

if [ "$STATUS1" -ne 0 ] || [ "$STATUS2" -ne 0 ] || [ "$STATUS3" -ne 0 ] || [ "$STATUS4" -ne 0 ]; then
    echo "FAIL: at least one client failed"

    echo "--- client1 stdout ---"
    cat /tmp/client1.out
    echo "--- client1 stderr ---"
    cat /tmp/client1.err

    echo "--- client2 stdout ---"
    cat /tmp/client2.out
    echo "--- client2 stderr ---"
    cat /tmp/client2.err

    echo "--- client3 stdout ---"
    cat /tmp/client3.out
    echo "--- client3 stderr ---"
    cat /tmp/client3.err

    echo "--- client4 stdout ---"
    cat /tmp/client4.out
    echo "--- client4 stderr ---"
    cat /tmp/client4.err

    echo "--- server output ---"
    cat "$SERVER_OUTPUT"

    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* \
        /tmp/client1.out /tmp/client2.out /tmp/client3.out /tmp/client4.out \
        /tmp/client1.err /tmp/client2.err /tmp/client3.err /tmp/client4.err
    exit 1
fi

if grep -q "Connection timed out" /tmp/client1.err /tmp/client2.err /tmp/client3.err /tmp/client4.err; then
    echo "FAIL: a client connection timed out"

    echo "--- client stderr ---"
    cat /tmp/client1.err /tmp/client2.err /tmp/client3.err /tmp/client4.err

    echo "--- server output ---"
    cat "$SERVER_OUTPUT"

    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* \
        /tmp/client1.out /tmp/client2.out /tmp/client3.out /tmp/client4.out \
        /tmp/client1.err /tmp/client2.err /tmp/client3.err /tmp/client4.err
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_* \
    /tmp/client1.out /tmp/client2.out /tmp/client3.out /tmp/client4.out \
    /tmp/client1.err /tmp/client2.err /tmp/client3.err /tmp/client4.err

exit 0