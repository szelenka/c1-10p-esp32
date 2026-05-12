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

# ============================================================================
# Shared variables (used by multiple .mk files)
# ============================================================================

CXX       := g++
CXXFLAGS  := -std=c++20 -I test/mocks -I main/include -pthread

PYTHON ?= $(shell command -v python3 2>/dev/null || command -v python 2>/dev/null)

CLANG_TIDY ?= $(shell command -v clang-tidy 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/clang-tidy)
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/clang-format)

TIDY_SRCS := $(wildcard main/chopper/**/*.cpp main/chopper/*.cpp)
TIDY_HDRS := $(wildcard main/include/chopper/**/*.h main/include/chopper/*.h)
TIDY_FLAGS := -- -std=c++20 -I test/mocks -I main/include

# ============================================================================
# Includes (order matters: coverage depends on test, gates depends on all)
# ============================================================================

include .make/vendor.mk
include .make/firmware.mk
include .make/test.mk
include .make/coverage.mk
include .make/lint.mk
include .make/checks.mk
include .make/ui.mk
include .make/gates.mk
