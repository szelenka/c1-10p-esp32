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
