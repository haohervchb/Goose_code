#!/bin/bash
# Bash Security Test Script for goosecode

cd /home/rah/goosecode

echo "=== Building goosecode ==="
make clean >/dev/null 2>&1
make 2>&1 | grep -E "(error|warning:.*bash_security)" | head -5

PASS=0
FAIL=0

echo ""
echo "=== Testing Bash Security ==="

test_bash() {
    local test_name="$1"
    local cmd="$2"
    local expected_pattern="$3"
    
    echo -n "Testing: $test_name ... "
    result=$(echo "{\"command\": \"$cmd\"}" | timeout 5 ./goosecode --json 2>&1 || true)
    
    if echo "$result" | grep -q "$expected_pattern"; then
        echo "PASS"
        ((PASS++))
    else
        echo "FAIL (got: $result)"
        ((FAIL++))
    fi
}

# Blocked tests (security checks)
test_bash "command substitution \$(whoami)" "\$(whoami)" "BLOCKED.*check_id=8"
test_bash "backticks" "`id`" "BLOCKED.*check_id=8"
test_bash "LD_PRELOAD" "LD_PRELOAD=x ls" "BLOCKED.*check_id=6"
test_bash "IFS injection" "IFS=: ls" "BLOCKED.*check_id=11"
test_bash "zmodload" "zmodload zsh/mapfile" "BLOCKED.*check_id=20"
test_bash "incomplete command -" "-la ls" "BLOCKED.*check_id=1"
test_bash "path override" "PATH=/tmp ls" "BLOCKED.*check_id=6"

# Allowed tests (should not be blocked)
test_bash "simple ls" "ls" "stdout\|ALLOWED"
test_bash "ls -la" "ls -la" "stdout\|ALLOWED"
test_bash "pwd" "pwd" "stdout\|ALLOWED"
test_bash "git status" "git status" "stdout\|ALLOWED"

echo ""
echo "=== Test Results ==="
echo "PASSED: $PASS"
echo "FAILED: $FAIL"
echo ""

if [ $FAIL -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
