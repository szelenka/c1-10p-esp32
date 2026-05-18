#!/usr/bin/env bash
# check_coverage.sh -- Enforce minimum coverage on changed production files.
# Called by: make check-coverage
#
# Uses llvm-cov to check that changed .cpp/.h files under main/ have at least
# the threshold line coverage. Requires coverage binaries to be built first.
set -euo pipefail

THRESHOLD="${COVERAGE_THRESHOLD:-60}"
COV_BIN="build/coverage"
COV_PROFDATA="$COV_BIN/merged.profdata"

if [ ! -f "$COV_PROFDATA" ]; then
    echo "SKIP: no merged.profdata found — run 'make test-coverage' first"
    exit 0
fi

# Get changed production files (headers and source)
CHANGED_PROD=$(git diff --name-only --diff-filter=d HEAD -- main/include/chopper/ main/chopper/ 2>/dev/null \
    | grep -E '\.(h|cpp)$' || true)

STAGED_PROD=$(git diff --name-only --diff-filter=d --cached -- main/include/chopper/ main/chopper/ 2>/dev/null \
    | grep -E '\.(h|cpp)$' || true)

ALL_CHANGED=$(echo "$CHANGED_PROD $STAGED_PROD" | tr ' ' '\n' | sort -u | grep -v '^$' || true)

if [ -z "$ALL_CHANGED" ]; then
    echo "PASS: no changed production files — coverage gate skipped"
    exit 0
fi

echo "== Coverage gate (threshold: ${THRESHOLD}%) =="
echo "Checking: $(echo "$ALL_CHANGED" | wc -l | tr -d ' ') changed file(s)"

# Get the primary integration binary for coverage report
PRIMARY_BIN="$COV_BIN/test_integration"
if [ ! -f "$PRIMARY_BIN" ]; then
    echo "SKIP: no coverage binary found — run 'make test-coverage' first"
    exit 0
fi

# Build object args for all coverage binaries
OBJECT_ARGS=""
for bin in "$COV_BIN"/test_*; do
    if [ -f "$bin" ] && [ "$bin" != "$PRIMARY_BIN" ]; then
        OBJECT_ARGS="$OBJECT_ARGS -object $bin"
    fi
done

FAILURES=0
for file in $ALL_CHANGED; do
    if [ ! -f "$file" ]; then
        continue
    fi

    # Get coverage for this specific file
    REPORT=$(xcrun llvm-cov report \
        "$PRIMARY_BIN" $OBJECT_ARGS \
        -instr-profile="$COV_PROFDATA" \
        "$file" 2>/dev/null || true)

    # Parse total line coverage from the TOTAL row
    TOTAL_LINE=$(echo "$REPORT" | grep 'TOTAL' || true)
    if [ -z "$TOTAL_LINE" ]; then
        echo "  NOTE: $file — no coverage data (may be header-only or untested)"
        continue
    fi

    # Extract the line coverage percentage (column varies, use the last percentage)
    COVERAGE=$(echo "$TOTAL_LINE" | awk '{for(i=NF;i>=1;i--) if($i ~ /%/) {gsub(/%/,"",$i); print $i; exit}}')

    if [ -z "$COVERAGE" ]; then
        echo "  NOTE: $file — could not parse coverage"
        continue
    fi

    # Compare as integers (truncate decimal)
    COV_INT=${COVERAGE%.*}
    if [ "$COV_INT" -lt "$THRESHOLD" ]; then
        echo "  WARN: $file — ${COVERAGE}% (below ${THRESHOLD}%)"
        FAILURES=$((FAILURES + 1))
    else
        echo "  PASS: $file — ${COVERAGE}%"
    fi
done

echo ""
if [ $FAILURES -gt 0 ]; then
    echo "== Coverage gate: $FAILURES file(s) below ${THRESHOLD}% threshold =="
    exit 1
else
    echo "== Coverage gate passed =="
fi
