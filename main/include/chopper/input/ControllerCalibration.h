#pragma once

#include <cstdint>

namespace chopper::input {

struct AxisCalibration {
    int32_t offset_x = 0;
    int32_t offset_y = 0;
    bool invert_x = false;
    bool invert_y = false;
};

inline int32_t applyAxisCalibrationRaw(int32_t raw, int32_t offset, bool inverted) {
    const int32_t oriented = inverted ? -raw : raw;
    return oriented + offset;
}

inline float normalizeControllerAxis(int32_t raw) {
    constexpr float kRange = 512.0f;
    float out = static_cast<float>(raw) / kRange;
    if (out > 1.0f) {
        out = 1.0f;
    }
    if (out < -1.0f) {
        out = -1.0f;
    }
    return out;
}

inline float normalizeCalibratedAxis(int32_t raw, int32_t offset, bool inverted) {
    return normalizeControllerAxis(applyAxisCalibrationRaw(raw, offset, inverted));
}

}  // namespace chopper::input
