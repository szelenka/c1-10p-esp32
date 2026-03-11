# ============================================================================
# Chopper — Main Makefile
# ============================================================================
#
# Target naming convention: verb-noun (e.g., check-safety, lint-tidy)
#   check-*   Validation gates (return 0/1)
#   lint-*    Static analysis
#   test-*    Test suites
#   format-*  Formatting
#   build/flash/monitor  Firmware actions
#   render-*  Generate output artifacts
#   report-*  Structured reports
#
# Script naming convention: verb_noun.{py,sh} matching the target name.
# Makefile targets delegate to .scripts/; inline shell kept to <5 lines.

include .make/vendor.mk

# ============================================================================
# Firmware build / flash / monitor
# ============================================================================

FIRMWARE_ENV ?= esp32dev
UPLOAD_PORT ?=

define run_firmware_build
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper (env=$(FIRMWARE_ENV))"; \
		./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)"; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI (env=$(FIRMWARE_ENV))"; \
		pio run -e "$(FIRMWARE_ENV)"; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py build"; \
		idf.py build; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

define run_firmware_flash
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper flash/monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t upload -t monitor; \
		else \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" -t upload -t monitor; \
		fi; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI flash/monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			pio run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t upload -t monitor; \
		else \
			pio run -e "$(FIRMWARE_ENV)" -t upload -t monitor; \
		fi; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py flash monitor"; \
		idf.py flash monitor; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

define run_firmware_monitor
	if [ -x "./.scripts/pio_local.sh" ]; then \
		echo "Using PlatformIO local wrapper monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t monitor; \
		else \
			./.scripts/pio_local.sh run -e "$(FIRMWARE_ENV)" -t monitor; \
		fi; \
	elif command -v pio >/dev/null 2>&1; then \
		echo "Using PlatformIO global CLI monitor (env=$(FIRMWARE_ENV))"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			pio run -e "$(FIRMWARE_ENV)" --upload-port "$(UPLOAD_PORT)" -t monitor; \
		else \
			pio run -e "$(FIRMWARE_ENV)" -t monitor; \
		fi; \
	elif command -v idf.py >/dev/null 2>&1; then \
		echo "Using ESP-IDF idf.py monitor"; \
		if [ -n "$(UPLOAD_PORT)" ]; then \
			idf.py -p "$(UPLOAD_PORT)" monitor; \
		else \
			idf.py monitor; \
		fi; \
	else \
		echo "Error: no supported firmware tool found (expected ./.scripts/pio_local.sh, pio, or idf.py)." >&2; \
		exit 1; \
	fi
endef

build:
	@set -e; $(run_firmware_build)

flash:
	@set -e; $(run_firmware_flash)

monitor:
	@set -e; $(run_firmware_monitor)

# ============================================================================
# Telemetry UI bridge
# ============================================================================

UI_HOST ?= 127.0.0.1
UI_PORT ?= 8765
UI_SERIAL ?=
UI_BAUD ?= 115200

run-ui-bridge:
	@set -e; \
	echo "Telemetry UI URL: http://$(UI_HOST):$(UI_PORT)"; \
	echo "Starting UI bridge (serial='$(UI_SERIAL)' baud=$(UI_BAUD))"; \
	cd tools/telemetry_ui; \
	if [ -x "./.venv/bin/python" ]; then \
		PY="./.venv/bin/python"; \
	elif [ -x "../.venv/bin/python" ]; then \
		PY="../.venv/bin/python"; \
	else \
		PY="python3"; \
	fi; \
	if ! $$PY -c "import aiohttp, serial" >/dev/null 2>&1; then \
		echo "Installing UI bridge dependencies..."; \
		$$PY -m pip install -r requirements.txt; \
	fi; \
	if [ -n "$(UI_SERIAL)" ]; then \
		CMD="$$PY app.py --host \"$(UI_HOST)\" --port \"$(UI_PORT)\" --serial \"$(UI_SERIAL)\" --baud \"$(UI_BAUD)\""; \
	else \
		CMD="$$PY app.py --host \"$(UI_HOST)\" --port \"$(UI_PORT)\" --baud \"$(UI_BAUD)\""; \
	fi; \
	set +e; \
	eval "$$CMD"; \
	RC=$$?; \
	set -e; \
	if [ $$RC -eq 130 ]; then \
		echo "UI bridge stopped (Ctrl+C)"; \
		exit 0; \
	fi; \
	exit $$RC

# ── URDF import from Fusion 360 export ──────────────────────────────────
FUSION_EXPORT_DIR ?=

import-urdf:
	@test -n "$(FUSION_EXPORT_DIR)" || { echo "Usage: make import-urdf FUSION_EXPORT_DIR=/path/to/fusion2urdf_description"; exit 1; }
	@test -d "$(FUSION_EXPORT_DIR)/urdf" || { echo "Error: $(FUSION_EXPORT_DIR)/urdf not found"; exit 1; }
	@test -d "$(FUSION_EXPORT_DIR)/meshes" || { echo "Error: $(FUSION_EXPORT_DIR)/meshes not found"; exit 1; }
	@echo "Importing meshes from $(FUSION_EXPORT_DIR)/meshes/ ..."
	rm -rf description/fusion2urdf/meshes
	mkdir -p description/fusion2urdf/meshes
	cp "$(FUSION_EXPORT_DIR)"/meshes/*.stl description/fusion2urdf/meshes/
	@echo "Resolving xacro -> URDF ..."
	python3 .scripts/xacro2urdf.py "$(FUSION_EXPORT_DIR)" description/fusion2urdf/chopper_fusion.urdf
	@echo ""
	@echo "Imported $$(ls description/fusion2urdf/meshes/*.stl | wc -l | tr -d ' ') STL meshes"
	@echo "URDF: description/fusion2urdf/chopper_fusion.urdf"
	@echo ""
	@echo "Check tools/telemetry_ui/joint_mapping.json if joint/link names changed."

# ============================================================================
# Host-side tests (no ESP-IDF required)
# ============================================================================

CXX       := g++
CXXFLAGS  := -std=c++20 -I test/mocks -I main/include -pthread
TEST_SRC  := test
TEST_BIN  := build/test

CORE_SRCS := \
	main/chopper/core/Node.cpp \
	main/chopper/core/Publisher.cpp \
	main/chopper/core/Subscription.cpp \
	main/chopper/core/MessageBroker.cpp \
	main/chopper/core/PublishingNode.cpp \
	main/chopper/core/Message.cpp

PARAM_SRC   := main/chopper/core/ParameterServer.cpp
TIMER_SRC   := main/chopper/core/TimerManager.cpp
EXEC_SRC    := main/chopper/core/Executor.cpp
DRIVER_SRC  := main/chopper/hal/DriverManager.cpp
SAFETY_SRCS := \
	main/chopper/safety/DegradationManager.cpp \
	main/chopper/safety/EmergencyStopChain.cpp \
	main/chopper/safety/SafetyManager.cpp
APP_SRC     := main/chopper/Application.cpp
TELEMETRY_SRC := main/chopper/TelemetryService.cpp

# Per-test source dependencies (test .cpp is prepended automatically)
DEPS_test_core                 := $(CORE_SRCS)
DEPS_test_safety               := $(SAFETY_SRCS)
DEPS_test_hal                  := $(DRIVER_SRC)
DEPS_test_bluetooth            :=
DEPS_test_message_enhancements := $(TIMER_SRC) $(PARAM_SRC)
DEPS_test_config               := $(PARAM_SRC)
DEPS_test_dome_ik              := $(CORE_SRCS) $(PARAM_SRC)
DEPS_test_maestro              := $(CORE_SRCS) $(PARAM_SRC)
DEPS_test_integration          := $(CORE_SRCS) $(EXEC_SRC) $(TIMER_SRC) $(PARAM_SRC) $(SAFETY_SRCS) $(DRIVER_SRC) $(APP_SRC) $(TELEMETRY_SRC)
DEPS_test_telemetry            := $(TELEMETRY_SRC)
DEPS_test_button_nodes         := $(CORE_SRCS) $(PARAM_SRC)
DEPS_test_sabertooth           :=
DEPS_test_mp3trigger           :=
DEPS_test_openmv_bridge        := $(CORE_SRCS)
DEPS_test_control_mapping      :=
DEPS_test_packet_to_action     := $(CORE_SRCS) $(PARAM_SRC)

# Auto-discover test names from test/test_*.cpp files
TEST_NAMES := $(sort $(basename $(notdir $(wildcard $(TEST_SRC)/test_*.cpp))))
TEST_BINS  := $(addprefix $(TEST_BIN)/,$(TEST_NAMES))

$(TEST_BIN):
	mkdir -p $(TEST_BIN)

# Generate build rules for each test binary
define make_test_rule
$(TEST_BIN)/$(1): $(TEST_SRC)/$(1).cpp $$(DEPS_$(1)) | $(TEST_BIN)
	$$(CXX) $$(CXXFLAGS) $$^ -o $$@
endef
$(foreach t,$(TEST_NAMES),$(eval $(call make_test_rule,$(t))))

check-test-inventory:
	@python3 ./.scripts/check_test_inventory.py

check-source-inventory:
	@python3 ./.scripts/check_source_inventory.py

test-build: check-test-inventory $(TEST_BINS)

test: check-test-inventory $(TEST_BINS)
	@failed=0; total=0; failed_names=""; \
	for bin in $(TEST_BINS); do \
		total=$$((total + 1)); \
		name=$$(basename $$bin); \
		printf "\n── %-40s ──\n" "$$name"; \
		out_file="$$(mktemp)"; \
		if $$bin > "$$out_file" 2>&1; then \
			grep -E "^(TEST:|\[doctest\]|=== Results:)" "$$out_file" || true; \
		else \
			echo ""; \
			grep -E "^(FAIL|TEST:.*FAIL|\[doctest\])" "$$out_file" || true; \
			grep -E "^(=== Results:|\[doctest\])" "$$out_file" || true; \
			failed=$$((failed + 1)); \
			failed_names="$$failed_names $$name"; \
		fi; \
		rm -f "$$out_file"; \
	done; \
	printf "\n══════════════════════════════════════════\n"; \
	printf "Suites: %d/%d passed\n" "$$((total - failed))" "$$total"; \
	if [ $$failed -gt 0 ]; then \
		printf "FAILED:$$failed_names\n"; \
	fi; \
	exit $$failed

clean-test:
	rm -rf $(TEST_BIN)

# ============================================================================
# Coverage (requires Apple Clang or LLVM toolchain)
# ============================================================================

COV_BIN     := build/coverage
COV_FLAGS   := -fprofile-instr-generate -fcoverage-mapping
COV_PROFDIR := $(COV_BIN)/profdata
COV_REPORT  := $(COV_BIN)/report
COV_BINS    := $(addprefix $(COV_BIN)/,$(TEST_NAMES))

$(COV_BIN):
	mkdir -p $(COV_BIN) $(COV_PROFDIR)

# Generate coverage build rules from the same DEPS_ declarations
define make_cov_rule
$(COV_BIN)/$(1): $(TEST_SRC)/$(1).cpp $$(DEPS_$(1)) | $(COV_BIN)
	$$(CXX) $$(CXXFLAGS) $$(COV_FLAGS) $$^ -o $$@
endef
$(foreach t,$(TEST_NAMES),$(eval $(call make_cov_rule,$(t))))

test-coverage: $(COV_BINS)
	@rm -rf $(COV_PROFDIR)/*.profraw
	@failed=0; \
	for bin in $(COV_BINS); do \
		name=$$(basename $$bin); \
		LLVM_PROFILE_FILE="$(COV_PROFDIR)/$$name.profraw" $$bin >/dev/null 2>&1 || failed=$$((failed + 1)); \
	done; \
	if [ $$failed -gt 0 ]; then \
		echo "WARNING: $$failed test suite(s) failed — coverage report will be partial"; \
	fi
	@echo "Merging profiles..."
	@xcrun llvm-profdata merge -sparse $(COV_PROFDIR)/*.profraw -o $(COV_BIN)/merged.profdata
	@echo "Generating HTML report..."
	@rm -rf $(COV_REPORT)
	@xcrun llvm-cov show \
		$(COV_BIN)/test_integration \
		$(addprefix -object ,$(filter-out $(COV_BIN)/test_integration,$(COV_BINS))) \
		-instr-profile=$(COV_BIN)/merged.profdata \
		-format=html \
		-output-dir=$(COV_REPORT) \
		-ignore-filename-regex='test/.*' \
		main/chopper/ main/include/chopper/
	@echo "Generating summary..."
	@xcrun llvm-cov report \
		$(COV_BIN)/test_integration \
		$(addprefix -object ,$(filter-out $(COV_BIN)/test_integration,$(COV_BINS))) \
		-instr-profile=$(COV_BIN)/merged.profdata \
		-ignore-filename-regex='test/.*' \
		main/chopper/ main/include/chopper/
	@echo ""
	@echo "HTML report: $(COV_REPORT)/index.html"

clean-coverage:
	rm -rf $(COV_BIN)

# ============================================================================
# Quality gates
# ============================================================================

CPPCHECK ?= cppcheck
CPPCHECK_FLAGS ?= --enable=warning,performance,portability --std=c++20 --quiet

CLANG_TIDY ?= $(shell command -v clang-tidy 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/clang-tidy)
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/clang-format)

TIDY_SRCS := $(wildcard main/chopper/**/*.cpp main/chopper/*.cpp)
TIDY_HDRS := $(wildcard main/include/chopper/**/*.h main/include/chopper/*.h)
TIDY_FLAGS := -- -std=c++20 -I test/mocks -I main/include

# Known baseline warning count (update after intentional changes)
TIDY_WARN_BASELINE := 25

lint-tidy:
	@echo "== clang-tidy =="
	@WARNS=$$( $(CLANG_TIDY) $(TIDY_SRCS) $(TIDY_FLAGS) 2>&1 \
		| grep -c 'warning:' || true ); \
	echo "Warnings: $$WARNS (baseline: $(TIDY_WARN_BASELINE))"; \
	if [ "$$WARNS" -gt "$(TIDY_WARN_BASELINE)" ]; then \
		echo "FAIL: $$WARNS warnings exceeds baseline of $(TIDY_WARN_BASELINE)"; \
		echo "Run 'make lint-tidy-detail' to see new warnings"; \
		exit 1; \
	else \
		echo "PASS"; \
	fi

lint-tidy-changed:
	@echo "== clang-tidy (changed files only) =="
	@CHANGED=$$(git diff --name-only --diff-filter=d HEAD -- main/chopper/ 2>/dev/null | grep '\.cpp$$' || true); \
	STAGED=$$(git diff --name-only --diff-filter=d --cached -- main/chopper/ 2>/dev/null | grep '\.cpp$$' || true); \
	ALL_CHANGED=$$(echo "$$CHANGED $$STAGED" | tr ' ' '\n' | sort -u | grep -v '^$$' || true); \
	if [ -z "$$ALL_CHANGED" ]; then \
		echo "No changed .cpp files under main/chopper/ — nothing to lint"; \
		echo "PASS"; \
	else \
		echo "Linting: $$ALL_CHANGED"; \
		WARNS=$$( $(CLANG_TIDY) $$ALL_CHANGED $(TIDY_FLAGS) 2>&1 \
			| grep -c 'warning:' || true ); \
		echo "Warnings in changed files: $$WARNS"; \
		if [ "$$WARNS" -gt 0 ]; then \
			$(CLANG_TIDY) $$ALL_CHANGED $(TIDY_FLAGS) 2>&1 | grep 'warning:'; \
			echo "WARN: $$WARNS warning(s) in changed files"; \
		else \
			echo "PASS"; \
		fi; \
	fi

lint-tidy-detail:
	@$(CLANG_TIDY) $(TIDY_SRCS) $(TIDY_FLAGS) 2>&1 | grep -v 'warnings generated'

lint-tidy-fix:
	@echo "== clang-tidy (auto-fix) =="
	@$(CLANG_TIDY) --fix --fix-errors $(TIDY_SRCS) $(TIDY_FLAGS)

lint-embedded:
	@echo "== Banned heap containers on hot path =="
	@HITS=$$(grep -rn \
		'std::string\|std::vector\|std::function\|std::unordered_map\|std::map\|std::set\|std::list\|std::deque' \
		main/include/chopper main/chopper 2>/dev/null \
		| grep -v '/adapters/' \
		| grep -vE ':[0-9]+:\s*//' \
		| grep -vE ':[0-9]+:\s*\*' \
		| grep -vE ':[0-9]+:\s*\*/' \
		| grep -vE '//.*std::' \
		| grep -vE 'NOLINT.*heap' || true); \
	if [ -n "$$HITS" ]; then echo "$$HITS"; echo "FAIL"; exit 1; else echo "PASS"; fi
	@echo "== Banned heap allocation calls =="
	@HITS=$$(grep -rnE '\bnew\s+\w|[^=]\bdelete\s|\bmalloc\s*\(|\bcalloc\s*\(|\brealloc\s*\(|\bfree\s*\(' \
		main/include/chopper main/chopper 2>/dev/null \
		| grep -v '/adapters/' \
		| grep -v '/hal/sw_serial' \
		| grep -vE ':[0-9]+:\s*//' \
		| grep -vE ':[0-9]+:\s*\*' \
		| grep -vE ':[0-9]+:\s*\*/' \
		| grep -vE '//.*\b(new|delete|malloc|free)\b' \
		| grep -vE 'NOLINT.*heap' || true); \
	if [ -n "$$HITS" ]; then echo "$$HITS"; echo "FAIL"; exit 1; else echo "PASS"; fi
	@echo "== Banned RTTI usage =="
	@HITS=$$(grep -rn 'dynamic_cast\|typeid' \
		main/include/chopper main/chopper 2>/dev/null \
		| grep -v '/adapters/' \
		| grep -vE ':[0-9]+:\s*//' \
		| grep -vE ':[0-9]+:\s*\*' \
		| grep -vE ':[0-9]+:\s*\*/' \
		| grep -vE '//.*dynamic_cast\|//.*typeid' \
		| grep -vE 'NOLINT.*heap' || true); \
	if [ -n "$$HITS" ]; then echo "$$HITS"; echo "FAIL"; exit 1; else echo "PASS"; fi

check-format:
	@echo "== clang-format (check) =="
	@if $(CLANG_FORMAT) --dry-run --Werror $(TIDY_SRCS) $(TIDY_HDRS) 2>&1; then \
		echo "PASS"; \
	else \
		echo "FAIL: run 'make format' to fix"; \
		exit 1; \
	fi

format:
	@echo "== clang-format (apply) =="
	@$(CLANG_FORMAT) -i $(TIDY_SRCS) $(TIDY_HDRS)
	@echo "Formatted $$(echo $(TIDY_SRCS) $(TIDY_HDRS) | wc -w | tr -d ' ') files"

check-safety:
	@./.scripts/check_safety.sh

check-safety-ordering:
	@python3 ./.scripts/check_safety_ordering.py

check-capacity:
	@python3 ./.scripts/check_capacity.py

check-reservation:
	@python3 ./.scripts/check_reservation.py

check-coverage:
	@./.scripts/check_coverage.sh

check-docs:
	@python3 ./.scripts/check_docs.py

check-triggers:
	@python3 ./.scripts/agent_scope.py --changed

plan-triggers:
	@python3 ./.scripts/agent_scope.py $(FILES)

check-handoff:
	@./.scripts/check_handoff.sh

check-traceability:
	./.scripts/check_traceability.sh docs/review/requirements_traceability_matrix.md

check-ui:
	@mkdir -p build/pycache
	@echo "── Python syntax check ──"
	@PYTHONPYCACHEPREFIX="$(CURDIR)/build/pycache" python3 -m py_compile tools/telemetry_ui/app.py
	@echo "PASS: app.py"
	@echo "── JS syntax check ──"
	@node --check tools/telemetry_ui/web/app.js
	@node --check tools/telemetry_ui/web/render3d.js 2>/dev/null || echo "SKIP: render3d.js (ES module, requires import map)"
	@echo "── Joint mapping validation ──"
	@$(MAKE) --no-print-directory check-ui-mapping
	@echo "── UI check complete ──"

check-ui-mapping:
	@python3 ./.scripts/check_ui_mapping.py

test-ui-integration:
	@echo "── Telemetry parser integration test ──"
	@python3 .scripts/test_ui_integration.py

analyze-cppcheck:
	$(CPPCHECK) $(CPPCHECK_FLAGS) -I main/include/chopper main/chopper main/include/chopper

render-agent-docs:
	@python3 ./.scripts/render_agent_docs.py

report-agent-gates:
	@python3 ./.scripts/report_agent_gates.py

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

# ============================================================================
# Composite gates
# ============================================================================

agent-gate-fast: check-environment test lint-tidy lint-embedded check-format check-safety check-docs check-traceability check-ui check-source-inventory

agent-gate-collab: agent-gate-fast check-handoff

agent-gate: agent-gate-fast

ci-local: agent-gate-fast analyze-cppcheck
