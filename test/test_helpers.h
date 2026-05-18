#pragma once
// Shared test macros for legacy (non-doctest) test suites.
// New tests should use doctest.h instead.

#include <cstdio>

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) \
    do { test_count++; std::printf("TEST: %s ... ", #name); } while (0)
#define PASS() \
    do { pass_count++; std::printf("PASS\n"); } while (0)
#define ASSERT(cond) \
    do { if (!(cond)) { std::printf("FAIL at %s:%d: %s\n", __FILE__, __LINE__, #cond); return; } } while (0)
