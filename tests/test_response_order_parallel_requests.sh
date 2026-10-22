#!/bin/bash

set -u

cd "$(dirname "$0")/.." || exit 1

rm -f FIFO_* users.txt /tmp/client_a.out /tmp/client_b.out

cat > users.txt <<EOF
Alice 10
Bob 10
EOF

SERVER_OUTPUT=$(mktemp)

./animate_server 4 > "$SERVER_OUTPUT" &
SERVER_PID=$!

sleep 1

(
    printf "Login Alice\ndestroy_canvas 0\ncreate_canvas 10 10 0\ndestroy_sprite 0\ncreate_rectangle 5 5 255 1\nDisconnect\n" \
        | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client_a.out
) &
PID1=$!

sleep 0.2

(
    printf "Login Bob\ndestroy_canvas 0\ncreate_canvas 20 20 0\ndestroy_sprite 0\ncreate_circle 3 255 1\nDisconnect\n" \
        | timeout 5s ./animate_client "$SERVER_PID" > /tmp/client_b.out
) &
PID2=$!

wait "$PID1" || true
wait "$PID2" || true

ACTUAL_A=$(tail -n +2 /tmp/client_a.out | head -n 4)
ACTUAL_B=$(tail -n +2 /tmp/client_b.out | head -n 4)

LINE_A1=$(echo "$ACTUAL_A" | sed -n '1p')
LINE_A2=$(echo "$ACTUAL_A" | sed -n '2p')
LINE_A3=$(echo "$ACTUAL_A" | sed -n '3p')
LINE_A4=$(echo "$ACTUAL_A" | sed -n '4p')

LINE_B1=$(echo "$ACTUAL_B" | sed -n '1p')
LINE_B2=$(echo "$ACTUAL_B" | sed -n '2p')
LINE_B3=$(echo "$ACTUAL_B" | sed -n '3p')
LINE_B4=$(echo "$ACTUAL_B" | sed -n '4p')

if [ "$LINE_A1" != "Value error" ] ||
   ! echo "$LINE_A2" | grep -q "Success [1-9]" ||
   [ "$LINE_A3" != "Value error" ] ||
   ! echo "$LINE_A4" | grep -q "Success [1-9]"; then
    echo "Alice responses were not ordered"
    cat /tmp/client_a.out
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* /tmp/client_a.out /tmp/client_b.out
    exit 1
fi

if [ "$LINE_B1" != "Value error" ] ||
   ! echo "$LINE_B2" | grep -q "Success [1-9]" ||
   [ "$LINE_B3" != "Value error" ] ||
   ! echo "$LINE_B4" | grep -q "Success [1-9]"; then
    echo "Bob responses were not ordered"
    cat /tmp/client_b.out
    kill "$SERVER_PID" 2>/dev/null
    rm -f "$SERVER_OUTPUT" users.txt FIFO_* /tmp/client_a.out /tmp/client_b.out
    exit 1
fi

kill "$SERVER_PID" 2>/dev/null
rm -f "$SERVER_OUTPUT" users.txt FIFO_* /tmp/client_a.out /tmp/client_b.out

exit 0