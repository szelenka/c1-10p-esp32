#!/usr/bin/env bash
# check_safety.sh -- Verify safety invariants in production code.
# Called by: make check-safety
#
# These are structural checks (does the code reference safety mechanisms?).
# They do NOT replace a manual behavioral audit.
set -euo pipefail

EXIT_CODE=0
WARN_COUNT=0
BRIDGE_DIR="main/include/chopper/nodes"

check_pass() { echo "PASS: $1"; }
check_fail() { echo "FAIL: $1"; EXIT_CODE=1; }
check_warn() { echo "WARN: $1"; WARN_COUNT=$((WARN_COUNT + 1)); }
check_skip() { echo "SKIP: $1"; }

echo "== Safety gate: MotorBridgeNode checks degradation =="
if [ -f "$BRIDGE_DIR/MotorBridgeNode.h" ]; then
    if grep -q 'degradation_\|DegradationManager\|getMode\|getAllowMotors\|safety_\|SafetyManager' "$BRIDGE_DIR/MotorBridgeNode.h"; then
        check_pass "MotorBridgeNode references safety gating"
    else
        check_warn "MotorBridgeNode has no degradation/safety reference -- verify gating happens upstream or add it"
    fi
else
    check_skip "MotorBridgeNode.h not found"
fi

echo ""
echo "== Safety gate: ServoBridgeNode checks degradation =="
if [ -f "$BRIDGE_DIR/ServoBridgeNode.h" ]; then
    if grep -q 'degradation_\|DegradationManager\|getMode\|getAllowServos\|safety_\|SafetyManager' "$BRIDGE_DIR/ServoBridgeNode.h"; then
        check_pass "ServoBridgeNode references safety gating"
    else
        check_warn "ServoBridgeNode has no degradation/safety reference -- verify gating happens upstream or add it"
    fi
else
    check_skip "ServoBridgeNode.h not found"
fi

echo ""
echo "== Emergency stop: bridge nodes implement emergencyStop() =="
for header in $(find "$BRIDGE_DIR" -name '*BridgeNode.h' -type f 2>/dev/null | sort); do
    node=$(basename "$header" .h)
    if grep -q 'emergencyStop' "$header"; then
        check_pass "$node implements emergencyStop()"
    else
        check_fail "$node missing emergencyStop()"
    fi
done

echo ""
echo "== DegradationManager starts at SAFE_STOP =="
DEG_FILES=$(find main -name 'DegradationManager*' -type f 2>/dev/null)
if [ -n "$DEG_FILES" ]; then
    if grep -q 'SAFE_STOP' $DEG_FILES; then
        check_pass "DegradationManager references SAFE_STOP initial state"
    else
        check_fail "DegradationManager may not start at SAFE_STOP"
    fi
else
    check_skip "DegradationManager not found"
fi

echo ""
echo "== SafetyManager runs before node processing =="
EXEC_FILES=$(find main -name 'Executor*' -type f 2>/dev/null)
if [ -n "$EXEC_FILES" ]; then
    if grep -q 'safety\|SafetyManager' $EXEC_FILES; then
        check_pass "Executor references safety system"
    else
        check_warn "Executor may not run safety checks (verify manually)"
    fi
else
    check_skip "Executor not found"
fi

echo ""
echo "== Safety gate ordering: delegating to check_safety_ordering.py =="
# Semantic ordering analysis (comment-aware) is handled by the Python script.
# This avoids duplicating fragile C++ parsing in bash.
if python3 ./.scripts/check_safety_ordering.py; then
    : # pass -- output already printed by Python script
else
    EXIT_CODE=1
fi

echo ""
if [ $EXIT_CODE -eq 0 ] && [ $WARN_COUNT -eq 0 ]; then
    echo "== All safety checks passed =="
elif [ $EXIT_CODE -eq 0 ]; then
    echo "== Safety checks passed with $WARN_COUNT warning(s) =="
else
    echo "== SAFETY FAILURES DETECTED =="
fi
exit $EXIT_CODE
