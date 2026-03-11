#!/usr/bin/env bash
# check_traceability.sh -- Validate requirements traceability matrix.
# Called by: make check-traceability
set -euo pipefail

MATRIX_FILE="${1:-docs/review/requirements_traceability_matrix.md}"

if [[ ! -f "$MATRIX_FILE" ]]; then
  echo "Traceability matrix not found: $MATRIX_FILE" >&2
  exit 1
fi

# Validate status values and require evidence fields for Met/Partial rows.
awk '
BEGIN {
  errors = 0;
}
/^\|/ {
  # Skip separator and header rows.
  if ($0 ~ /^\|---/ || $0 ~ /Requirement Area \(docs\/INSTRUCTIONS\.md\)/) next;

  n = split($0, c, "|");
  # Expected markdown table shape: leading + 5 columns + trailing pipe.
  if (n < 7) next;

  req = c[2];
  status = c[3];
  code = c[4];
  tests = c[5];

  gsub(/^ +| +$/, "", req);
  gsub(/^ +| +$/, "", status);
  gsub(/^ +| +$/, "", code);
  gsub(/^ +| +$/, "", tests);

  if (status != "Met" && status != "Partial" && status != "Planned" && status != "Not Evidenced") {
    print "Invalid status in matrix row: " req " -> " status > "/dev/stderr";
    errors++;
  }

  if ((status == "Met" || status == "Partial") && (code == "" || tests == "")) {
    print "Missing evidence for in-scope row: " req > "/dev/stderr";
    errors++;
  }
}
END {
  if (errors > 0) {
    exit 1;
  }
}
' "$MATRIX_FILE"

echo "Traceability check passed: $MATRIX_FILE"
