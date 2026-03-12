# ============================================================================
# Validation gates, analysis, and reports
# ============================================================================

CPPCHECK ?= cppcheck
CPPCHECK_FLAGS ?= --enable=warning,performance,portability --std=c++20 --quiet

check-assertions:
	@python3 ./.scripts/check_assertions.py

check-high-impact:
	@python3 ./.scripts/check_high_impact.py

check-placement:
	@python3 ./.scripts/check_placement.py

check-safety:
	@./.scripts/check_safety.sh

check-safety-ordering:
	@python3 ./.scripts/check_safety_ordering.py

check-safety-paths:
	@./.scripts/check_safety_paths.sh

check-safety-reachability:
	@python3 ./.scripts/check_safety_reachability.py

check-capacity:
	@python3 ./.scripts/check_capacity.py

check-coverage:
	@./.scripts/check_coverage.sh

check-docs:
	@python3 ./.scripts/check_docs.py

sync-registry:
	@python3 ./.scripts/sync_registry.py --write

check-triggers:
	@python3 ./.scripts/agent_scope.py --changed

plan-triggers:
	@python3 ./.scripts/agent_scope.py $(FILES)

check-traceability:
	./.scripts/check_traceability.sh docs/review/requirements_traceability_matrix.md

check-environment:
	@OK=true; \
	for cmd in $(CLANG_TIDY) $(CLANG_FORMAT) python3 node; do \
		if ! command -v "$$cmd" >/dev/null 2>&1; then \
			echo "FAIL: required tool not found: $$cmd"; \
			OK=false; \
		fi; \
	done; \
	python3 -c "import yaml" 2>/dev/null || { echo "FAIL: Python pyyaml module not installed (pip install pyyaml)"; OK=false; }; \
	$$OK && echo "PASS: all required tools available" || exit 1

analyze-cppcheck:
	$(CPPCHECK) $(CPPCHECK_FLAGS) -I main/include/chopper main/chopper main/include/chopper

render-agent-docs:
	@python3 ./.scripts/render_agent_docs.py

check-generated-docs:
	@python3 ./.scripts/render_agent_docs.py --check

check-routing-consistency:
	@python3 ./.scripts/render_agent_docs.py --check

check-agent-instructions:
	@python3 ./.scripts/render_agent_docs.py --check

report-done-when:
	@python3 ./.scripts/report_done_when.py $(ROLES)

report-agent-gates:
	@python3 ./.scripts/report_agent_gates.py
