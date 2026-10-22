#!/bin/bash

set -u

# Find the tests directory and project root without depending on where make was run.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PASSED=0
FAILED=0

echo "Running tests..."
echo

# Run tests from the tests directory so relative paths stay predictable.
cd "$SCRIPT_DIR" || exit 1

for test_file in test_*.sh; do
    if [ ! -f "$test_file" ]; then
        echo "No test files found."
        exit 1
    fi

    test_name="${test_file%.sh}"

    echo "==> Running $test_name"

    # Each test script controls its own setup, assertions, and cleanup.
    if bash "$test_file"; then
        echo "PASS: $test_name"
        PASSED=$((PASSED + 1))
    else
        echo "FAIL: $test_name"
        FAILED=$((FAILED + 1))
    fi

    echo
done

echo "Tests finished."
echo "Passed: $PASSED"
echo "Failed: $FAILED"

# make test should fail if even one individual test fails.
if [ "$FAILED" -ne 0 ]; then
    exit 1
fi

exit 0