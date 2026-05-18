# ============================================================================
# Composite gates
# ============================================================================

preflight: check-test-inventory test-build check-capacity
	@echo "── Pre-flight git status ──"
	@git status --short
	@echo "── Pre-flight complete ──"

agent-gate-fast: check-environment test lint-tidy lint-embedded check-format check-assertions check-placement check-safety check-safety-ordering check-safety-paths check-safety-reachability check-docs check-traceability check-ui check-source-inventory check-triggers check-generated-docs

agent-gate: agent-gate-fast

# Structured gate runner — emits GATE: lines and a GATE_SUMMARY JSON line
agent-gate-json:
	@$(PYTHON) .scripts/run_gates.py

ci-local: agent-gate-fast analyze-cppcheck
