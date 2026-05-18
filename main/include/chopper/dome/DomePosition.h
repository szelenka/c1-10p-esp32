#pragma once

#include <cmath>
#include <cstdint>

namespace chopper::dome {

/**
 * Dome position tracking and mode management.
 *
 * Tracks dome angle via an external sensor value fed through update(),
 * computes shortest-distance rotations, and manages dome modes
 * (Off, Home, Random, Target).
 *
 * No framework dependencies — callers provide time and sensor readings.
 */
class DomePosition {
public:
    enum Mode : uint8_t {
        kOff = 1,
        kHome,
        kRandom,
        kTarget
    };

    DomePosition() = default;

    // --- Sensor interface ---

    /**
     * Update the dome position from sensor.
     * @param angle   Current dome angle (0-359 degrees).
     * @param now_ms  Current time in milliseconds.
     */
    void update(unsigned angle, uint64_t now_ms) {
        fReady = true;
        if (angle != fLastAngle) {
            if (fLastAngle < angle) {
                fRelativeDegrees += shortestDistance(fLastAngle, angle);
            } else {
                fRelativeDegrees -= shortestDistance(angle, fLastAngle);
            }
            fLastChangeMS = now_ms;
            fLastAngle = angle;
        }
    }

    [[nodiscard]] bool ready() const { return fReady; }
    [[nodiscard]] unsigned getDomePosition() const { return fLastAngle; }
    [[nodiscard]] int getRelativeDegrees() const { return fRelativeDegrees; }

    // --- Mode management ---

    [[nodiscard]] Mode getDomeMode() const {
        if (!fReady) {
            return kOff;
        }
        return fDomeMode;
    }

    void setDomeMode(Mode mode, uint64_t now_ms) {
        fDomeMode = mode;
        fLastChangeMS = now_ms;
    }

    [[nodiscard]] Mode getDomeDefaultMode() const { return fDomeDefaultMode; }

    void setDomeDefaultMode(Mode mode, uint64_t now_ms) {
        fDomeDefaultMode = mode;
        setDomeMode(mode, now_ms);
    }

    // --- Speed ---

    [[nodiscard]] float getDomeSpeed() const {
        switch (getDomeMode()) {
            case kHome:
                return getDomeSpeedHome();
            case kRandom:
                return getDomeAutoSpeed();
            case kTarget:
                return getDomeSpeedTarget();
            case kOff:
            default:
                return getDomeMinSpeed();
        }
    }

    [[nodiscard]] float getDomeSpeedHome() const { return float(fDomeSpeedHome) / 100.0f; }
    [[nodiscard]] float getDomeSpeedTarget() const { return float(fDomeSpeedTarget) / 100.0f; }
    [[nodiscard]] float getDomeMinSpeed() const { return float(fDomeSpeedMin) / 100.0f; }
    [[nodiscard]] float getDomeAutoSpeed() const { return float(fDomeSpeedAuto) / 100.0f; }

    // --- Random movement range ---

    [[nodiscard]] unsigned getDomeAutoLeft() const { return fDomeAutoLeft; }
    [[nodiscard]] unsigned getDomeAutoRight() const { return fDomeAutoRight; }

    void setDomeAutoLeftDegrees(uint8_t degrees) { fDomeAutoLeft = degrees; }
    void setDomeAutoRightDegrees(uint8_t degrees) { fDomeAutoRight = degrees; }

    // --- Delay configuration (per-mode min/max seconds between movements) ---

    [[nodiscard]] unsigned getDomeAutoMinDelay() const { return fDomeAutoMinDelay; }
    [[nodiscard]] unsigned getDomeAutoMaxDelay() const { return fDomeAutoMaxDelay; }
    [[nodiscard]] unsigned getDomeHomeMinDelay() const { return fDomeHomeMinDelay; }
    [[nodiscard]] unsigned getDomeHomeMaxDelay() const { return fDomeHomeMaxDelay; }
    [[nodiscard]] unsigned getDomeTargetMinDelay() const { return fDomeTargetMinDelay; }
    [[nodiscard]] unsigned getDomeTargetMaxDelay() const { return fDomeTargetMaxDelay; }

    void setDomeAutoMinDelay(uint8_t sec) { fDomeAutoMinDelay = sec; }
    void setDomeAutoMaxDelay(uint8_t sec) { fDomeAutoMaxDelay = sec; }
    void setDomeHomeMinDelay(uint8_t sec) { fDomeHomeMinDelay = sec; }
    void setDomeHomeMaxDelay(uint8_t sec) { fDomeHomeMaxDelay = sec; }
    void setDomeTargetMinDelay(uint8_t sec) { fDomeTargetMinDelay = sec; }
    void setDomeTargetMaxDelay(uint8_t sec) { fDomeTargetMaxDelay = sec; }

    /** Get the min delay for the current mode. */
    [[nodiscard]] unsigned getDomeMinDelay() const {
        switch (getDomeMode()) {
            case kHome:
                return getDomeHomeMinDelay();
            case kRandom:
                return getDomeAutoMinDelay();
            case kTarget:
                return getDomeTargetMinDelay();
            case kOff:
            default:
                return 0;
        }
    }

    /** Get the max delay for the current mode. */
    [[nodiscard]] unsigned getDomeMaxDelay() const {
        switch (getDomeMode()) {
            case kHome:
                return getDomeHomeMaxDelay();
            case kRandom:
                return getDomeAutoMaxDelay();
            case kTarget:
                return getDomeTargetMaxDelay();
            case kOff:
            default:
                return 0;
        }
    }

    // --- Position helpers ---

    [[nodiscard]] unsigned getDomeHome() const { return fDomeHome; }
    [[nodiscard]] unsigned getDomeTargetPosition() const { return fDomeTargetPos; }
    [[nodiscard]] long getDomeRelativeTargetPosition() const { return fDomeRelativeTargetPos; }
    [[nodiscard]] unsigned getDomeFudge() const { return fDomeFudge; }

    void setDomeHomePosition(long degrees) { fDomeHome = normalize(degrees); }

    void setDomeTargetPosition(long degrees) {
        fDomeTargetPos = normalize(degrees);
        fDomeRelativeTargetPos = 0;
    }

    void setDomeRelativeTargetPosition(long degrees) {
        fDomeTargetPos = getDomePosition();
        fDomeRelativeTargetPos = degrees;
        fRelativeDegrees = 0;
    }

    void setDomeHomeRelativeTargetPosition(long degrees) { setDomeTargetPosition(degrees + getDomeHome()); }

    void setDomeHomeRelativeHomePosition(long degrees) { fDomeHome = normalize(degrees + getDomeHome()); }

    [[nodiscard]] unsigned getHomeRelativeDomePosition() const {
        return normalize(long(getDomePosition()) - long(getDomeHome()));
    }

    [[nodiscard]] bool isAtPosition(long degrees) const {
        long fudge = getDomeFudge();
        degrees = normalize(degrees);
        return withinArc(static_cast<float>(degrees - fudge), static_cast<float>(degrees + fudge),
                         static_cast<float>(getDomePosition()));
    }

    [[nodiscard]] bool isTimeout(uint64_t now_ms) const {
        if (!fReady || fLastAngle == ~0u) {
            return true;
        }
        return uint64_t(fTimeout) * 1000 < (now_ms - fLastChangeMS);
    }

    void resetDefaultMode(uint64_t now_ms) {
        setDomeMode(getDomeDefaultMode(), now_ms);
        fDomeRelativeTargetPos = 0;
        fRelativeDegrees = 0;
    }

    void resetWatchdog(uint64_t now_ms) { fLastChangeMS = now_ms; }

    void setTimeout(uint8_t timeout) { fTimeout = timeout; }

    // --- Setters for tuning parameters ---
    void setDomeHomeSpeed(uint8_t speed) { fDomeSpeedHome = speed; }
    void setDomeTargetSpeed(uint8_t speed) { fDomeSpeedTarget = speed; }
    void setDomeMinSpeed(uint8_t speed) { fDomeSpeedMin = speed; }
    void setDomeAutoSpeed(uint8_t speed) { fDomeSpeedAuto = speed; }
    void setDomeFudgeFactor(uint8_t fudge) { fDomeFudge = fudge; }

    // --- Shortest-distance rotation ---
    static int shortestDistance(int origin, int target) {
        int diff = std::abs(origin - target) % 360;
        int result = 0;
        if (diff > 180) {
            result = 360 - diff;
            if (target > origin) {
                result *= -1;
            }
        } else {
            result = diff;
            if (origin > target) {
                result *= -1;
            }
        }
        return result;
    }

    static unsigned normalize(long degrees) {
        degrees = degrees % 360;
        if (degrees < 0) {
            degrees += 360;
        }
        return static_cast<unsigned>(degrees);
    }

private:
    static bool withinArc(float p1, float p2, float p3) {
        return std::fmod(p2 - p1 + 720.0f, 360.0f) >= std::fmod(p3 - p1 + 720.0f, 360.0f);
    }

    Mode fDomeMode = kOff;
    Mode fDomeDefaultMode = kOff;
    bool fReady = false;
    uint16_t fDomeHome = 0;
    uint16_t fDomeTargetPos = 0;
    long fDomeRelativeTargetPos = 0;
    uint8_t fDomeAutoMinDelay = 6;
    uint8_t fDomeAutoMaxDelay = 8;
    uint8_t fDomeHomeMinDelay = 6;
    uint8_t fDomeHomeMaxDelay = 8;
    uint8_t fDomeTargetMinDelay = 6;
    uint8_t fDomeTargetMaxDelay = 8;
    uint8_t fDomeAutoRight = 80;
    uint8_t fDomeAutoLeft = 80;
    uint8_t fDomeFudge = 5;
    uint8_t fDomeSpeedHome = 40;
    uint8_t fDomeSpeedTarget = 40;
    uint8_t fDomeSpeedMin = 15;
    uint8_t fDomeSpeedAuto = 30;
    uint8_t fTimeout = 5;
    unsigned fLastAngle = ~0u;
    uint64_t fLastChangeMS = 0;
    int fRelativeDegrees = 0;
};

}  // namespace chopper::dome
