# ============================================================================
# Host-side tests (no ESP-IDF required)
# ============================================================================

TEST_SRC  := test
TEST_BIN  := build/test

CORE_SRCS := \
	main/chopper/core/Node.cpp \
	main/chopper/core/Publisher.cpp \
	main/chopper/core/Subscription.cpp \
	main/chopper/core/MessageBroker.cpp \
	main/chopper/core/PublishingNode.cpp \
	main/chopper/core/Message.cpp \
	main/chopper/core/ParameterServer.cpp

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
DEPS_test_dome_ik              := $(CORE_SRCS)
DEPS_test_maestro              := $(CORE_SRCS)
DEPS_test_integration          := $(CORE_SRCS) $(EXEC_SRC) $(TIMER_SRC) $(SAFETY_SRCS) $(DRIVER_SRC) $(APP_SRC) $(TELEMETRY_SRC)
DEPS_test_telemetry            := $(TELEMETRY_SRC)
DEPS_test_button_nodes         := $(CORE_SRCS)
DEPS_test_sabertooth           :=
DEPS_test_mp3trigger           :=
DEPS_test_openmv_bridge        := $(CORE_SRCS) $(PARAM_SRC)
DEPS_test_control_mapping      :=
DEPS_test_packet_to_action     := $(CORE_SRCS)
DEPS_test_rss_servo_limits     := $(CORE_SRCS)

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
	@$(PYTHON) ./.scripts/check_test_inventory.py

check-source-inventory:
	@$(PYTHON) ./.scripts/check_source_inventory.py

test-build: check-test-inventory $(TEST_BINS)

KNOWN_FAILURES := test/known_failures.txt

test: check-test-inventory $(TEST_BINS)
	@failed=0; total=0; failed_names=""; passed_names=""; \
	for bin in $(TEST_BINS); do \
		total=$$((total + 1)); \
		name=$$(basename $$bin); \
		printf "\n── %-40s ──\n" "$$name"; \
		out_file="$$(mktemp)"; \
		if $$bin > "$$out_file" 2>&1; then \
			grep -E "^(TEST:|\[doctest\]|=== Results:)" "$$out_file" || true; \
			passed_names="$$passed_names $$name"; \
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
	known=""; unexpected=0; unexpected_names=""; stale_names=""; \
	if [ -f "$(KNOWN_FAILURES)" ]; then \
		known=$$(grep -v '^\s*#' "$(KNOWN_FAILURES)" | grep -v '^\s*$$' || true); \
	fi; \
	for name in $$failed_names; do \
		if echo "$$known" | grep -qx "$$name" 2>/dev/null; then \
			printf "  %-40s (known failure)\n" "$$name"; \
		else \
			unexpected=$$((unexpected + 1)); \
			unexpected_names="$$unexpected_names $$name"; \
		fi; \
	done; \
	for name in $$known; do \
		if echo "$$passed_names" | grep -q "$$name" 2>/dev/null; then \
			stale_names="$$stale_names $$name"; \
		fi; \
	done; \
	if [ -n "$$stale_names" ]; then \
		printf "STALE known failures (tests now pass — remove from %s):%s\n" "$(KNOWN_FAILURES)" "$$stale_names"; \
		stale=1; \
	else \
		stale=0; \
	fi; \
	if [ $$unexpected -gt 0 ]; then \
		printf "UNEXPECTED FAILURES:%s\n" "$$unexpected_names"; \
	fi; \
	if [ $$stale -gt 0 ]; then \
		printf "FAIL: stale known_failures entries must be removed\n"; \
	fi; \
	exit $$((unexpected + stale))

clean-test:
	rm -rf $(TEST_BIN)
