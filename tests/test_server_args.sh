#!/bin/bash

set -u

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

make > /dev/null

if ./animate_server > /dev/null 2>&1; then
    echo "Server accepted missing argument"
    exit 1
fi

if ./animate_server abc > /dev/null 2>&1; then
    echo "Server accepted non-integer argument"
    exit 1
fi

if ./animate_server 0 > /dev/null 2>&1; then
    echo "Server accepted zero threadpool size"
    exit 1
fi

if ./animate_server -1 > /dev/null 2>&1; then
    echo "Server accepted negative threadpool size"
    exit 1
fi

timeout 1 ./animate_server 1 > /dev/null 2>&1
code=$?

if [ "$code" -ne 124 ]; then
    echo "Server did not stay alive with valid threadpool size"
    exit 1
fi

exit 0
