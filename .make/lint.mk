# ============================================================================
# Linting and formatting
# ============================================================================

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
	@CHANGED_CPP=$$(git diff --name-only --diff-filter=d HEAD -- main/chopper/ 2>/dev/null | grep '\.cpp$$' || true); \
	STAGED_CPP=$$(git diff --name-only --diff-filter=d --cached -- main/chopper/ 2>/dev/null | grep '\.cpp$$' || true); \
	CHANGED_HDR=$$(git diff --name-only --diff-filter=d HEAD -- main/include/chopper/ 2>/dev/null | grep '\.h$$' || true); \
	STAGED_HDR=$$(git diff --name-only --diff-filter=d --cached -- main/include/chopper/ 2>/dev/null | grep '\.h$$' || true); \
	ALL_CPP=$$(echo "$$CHANGED_CPP $$STAGED_CPP" | tr ' ' '\n' | sort -u | grep -v '^$$' || true); \
	ALL_HDR=$$(echo "$$CHANGED_HDR $$STAGED_HDR" | tr ' ' '\n' | sort -u | grep -v '^$$' || true); \
	if [ -n "$$ALL_HDR" ] && [ -z "$$ALL_CPP" ]; then \
		echo "Changed headers detected — linting all .cpp to catch header warnings:"; \
		echo "  $$ALL_HDR"; \
		ALL_CPP="$(TIDY_SRCS)"; \
	fi; \
	if [ -z "$$ALL_CPP" ]; then \
		echo "No changed .cpp or .h files under main/ — nothing to lint"; \
		echo "PASS"; \
	else \
		echo "Linting: $$ALL_CPP"; \
		if [ -n "$$ALL_HDR" ]; then echo "  (triggered by header changes: $$ALL_HDR)"; fi; \
		WARNS=$$( $(CLANG_TIDY) $$ALL_CPP $(TIDY_FLAGS) 2>&1 \
			| grep -c 'warning:' || true ); \
		echo "Warnings in changed files: $$WARNS"; \
		if [ "$$WARNS" -gt 0 ]; then \
			$(CLANG_TIDY) $$ALL_CPP $(TIDY_FLAGS) 2>&1 | grep 'warning:'; \
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
