#pragma once

// Portable math constants — POSIX M_PI etc. are not available on all platforms.
namespace chopper::math {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kPiOver2 = 1.57079632679489661923f;
static constexpr float kPiOver4 = 0.78539816339744830962f;

}  // namespace chopper::math
