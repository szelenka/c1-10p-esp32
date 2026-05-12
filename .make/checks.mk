# ============================================================================
# Validation gates, analysis, and reports
# ============================================================================

CPPCHECK ?= cppcheck
CPPCHECK_FLAGS ?= --enable=warning,performance,portability --std=c++20 --quiet

check-assertions:
	@$(PYTHON) ./.scripts/check_assertions.py

check-high-impact:
	@$(PYTHON) ./.scripts/check_high_impact.py

check-placement:
	@$(PYTHON) ./.scripts/check_placement.py

check-safety:
	@./.scripts/check_safety.sh

check-safety-ordering:
	@$(PYTHON) ./.scripts/check_safety_ordering.py

check-safety-paths:
	@./.scripts/check_safety_paths.sh

check-safety-reachability:
	@$(PYTHON) ./.scripts/check_safety_reachability.py

check-capacity:
	@$(PYTHON) ./.scripts/check_capacity.py

check-coverage:
	@./.scripts/check_coverage.sh

check-docs:
	@$(PYTHON) ./.scripts/check_docs.py

sync-registry:
	@$(PYTHON) ./.scripts/sync_registry.py --write

check-triggers:
	@$(PYTHON) ./.scripts/agent_scope.py --changed

plan-triggers:
	@$(PYTHON) ./.scripts/agent_scope.py $(FILES)

check-traceability:
	./.scripts/check_traceability.sh docs/review/requirements_traceability_matrix.md

check-environment:
	@OK=true; \
	for cmd in $(CLANG_TIDY) $(CLANG_FORMAT) $(PYTHON) node; do \
		if ! command -v "$$cmd" >/dev/null 2>&1; then \
			echo "FAIL: required tool not found: $$cmd"; \
			OK=false; \
		fi; \
	done; \
	$(PYTHON) -c "import yaml" 2>/dev/null || { echo "FAIL: Python pyyaml module not installed (pip install pyyaml)"; OK=false; }; \
	$$OK && echo "PASS: all required tools available" || exit 1

analyze-cppcheck:
	$(CPPCHECK) $(CPPCHECK_FLAGS) -I main/include/chopper main/chopper main/include/chopper

render-agent-docs:
	@$(PYTHON) ./.scripts/render_agent_docs.py

check-generated-docs:
	@$(PYTHON) ./.scripts/render_agent_docs.py --check

check-routing-consistency:
	@$(PYTHON) ./.scripts/render_agent_docs.py --check

check-agent-instructions:
	@$(PYTHON) ./.scripts/render_agent_docs.py --check

report-done-when:
	@$(PYTHON) ./.scripts/report_done_when.py $(ROLES)

report-agent-gates:
	@$(PYTHON) ./.scripts/report_agent_gates.py
