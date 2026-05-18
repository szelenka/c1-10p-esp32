#!/usr/bin/env bash
# check_safety_paths.sh — detect new publish calls to */cmd topics
# and verify they route through a safety-gated bridge node.
#
# This is a heuristic grep-based check. It catches the 90% case:
# any file that publishes to a topic ending in /cmd should have
# a corresponding bridge node subscription for that topic.

set -euo pipefail

FIRMWARE_DIR="main/include/chopper"
EXIT_CODE=0
WARN_COUNT=0

echo "== Safety path check: */cmd topic publishers =="

# 1. Find all createPublisher calls with topics containing /cmd
CMD_PUBLISHERS=$(grep -rn 'createPublisher.*"[a-z_]*/cmd"' "$FIRMWARE_DIR" 2>/dev/null || true)

if [ -z "$CMD_PUBLISHERS" ]; then
    echo "No */cmd topic publishers found."
    echo "PASS"
    exit 0
fi

# 2. Extract unique topic names from publisher calls
TOPICS=$(echo "$CMD_PUBLISHERS" \
    | grep -oE '"[a-z_/]+/cmd"' \
    | sort -u \
    | tr -d '"')

# 3. For each cmd topic, verify a bridge node subscribes to it
BRIDGE_DIR="$FIRMWARE_DIR/nodes"
UNGATCHED=""

for topic in $TOPICS; do
    # Check if any bridge node subscribes to this topic
    BRIDGE_SUB=$(grep -rl "createSubscription.*\"$topic\"" "$BRIDGE_DIR"/*Bridge*.h 2>/dev/null || true)
    # Also check TelemetryIOTapNode (passive observer, acceptable)
    TAP_SUB=$(grep -rl "\"$topic\"" "$BRIDGE_DIR"/TelemetryIOTapNode.h 2>/dev/null || true)

    if [ -z "$BRIDGE_SUB" ] && [ -z "$TAP_SUB" ]; then
        UNGATCHED="$UNGATCHED\n  $topic"
    fi
done

if [ -n "$UNGATCHED" ]; then
    echo "WARNING: The following */cmd topics have publishers but no bridge node subscriber:"
    echo -e "$UNGATCHED"
    echo ""
    echo "Every command topic reaching a motor/servo must route through a"
    echo "safety-gated bridge node (MotorBridgeNode, ServoBridgeNode, etc.)."
    echo ""
    # Find which files publish to these ungatched topics
    for topic in $(echo -e "$UNGATCHED" | tr -d ' '); do
        [ -z "$topic" ] && continue
        echo "Publishers of '$topic':"
        grep -rn "createPublisher.*\"$topic\"" "$FIRMWARE_DIR" 2>/dev/null | sed 's/^/  /'
    done
    echo ""
    echo "WARN"
    WARN_COUNT=$((WARN_COUNT + 1))
else
    echo "All */cmd topics route through bridge nodes."
    echo "PASS"
fi

# 4. Check for direct driver method calls outside bridge nodes
echo ""
echo "== Direct driver calls outside bridge nodes =="
DIRECT_CALLS=$(grep -rn '->setSpeed\|->setPosition\|->setTarget\|->setPulse\|->sendCommand' \
    "$FIRMWARE_DIR/nodes/" 2>/dev/null \
    | grep -v 'BridgeNode' \
    | grep -v 'DriverUpdateNode' \
    | grep -v '//' || true)

if [ -n "$DIRECT_CALLS" ]; then
    echo "WARNING: Direct driver calls found outside bridge nodes:"
    echo "$DIRECT_CALLS" | sed 's/^/  /'
    echo ""
    echo "Driver commands must go through bridge nodes for safety gating."
    echo "WARN"
    WARN_COUNT=$((WARN_COUNT + 1))
else
    echo "No direct driver calls outside bridge nodes."
    echo "PASS"
fi

if [ $EXIT_CODE -gt 0 ]; then
    echo ""
    echo "== SAFETY PATH FAILURES DETECTED =="
elif [ $WARN_COUNT -gt 0 ]; then
    echo ""
    echo "== Safety path check passed with $WARN_COUNT warning(s) =="
fi
exit $EXIT_CODE
