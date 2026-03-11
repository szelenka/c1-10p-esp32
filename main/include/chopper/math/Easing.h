#pragma once

#include <cmath>
#include <cstdint>

namespace chopper {
namespace math {

/**
 * 32 easing functions for smooth servo/motor interpolation.
 * All are pure math — no framework dependencies.
 *
 * Each function takes a completion ratio p in [0, 1] and returns
 * an eased value in approximately [0, 1].
 */
class Easing {
public:
    using Method = float (*)(float);

    enum EasingType : uint8_t {
        kLinearInterpolation = 0,
        kContinuous = 1,
        kQuadraticEaseIn = 2,
        kQuadraticEaseOut = 3,
        kQuadraticEaseInOut = 4,
        kCubicEaseIn = 5,
        kCubicEaseOut = 6,
        kCubicEaseInOut = 7,
        kQuarticEaseIn = 8,
        kQuarticEaseOut = 9,
        kQuarticEaseInOut = 10,
        kQuinticEaseIn = 11,
        kQuinticEaseOut = 12,
        kQuinticEaseInOut = 13,
        kSineEaseIn = 14,
        kSineEaseOut = 15,
        kSineEaseInOut = 16,
        kCircularEaseIn = 17,
        kCircularEaseOut = 18,
        kCircularEaseInOut = 19,
        kExponentialEaseIn = 20,
        kExponentialEaseOut = 21,
        kExponentialEaseInOut = 22,
        kElasticEaseIn = 23,
        kElasticEaseOut = 24,
        kElasticEaseInOut = 25,
        kBackEaseIn = 26,
        kBackEaseOut = 27,
        kBackEaseInOut = 28,
        kBounceEaseIn = 29,
        kBounceEaseOut = 30,
        kBounceEaseInOut = 31
    };

    static float LinearInterpolation(float p) { return p; }
    static float Continuous(float p) { return p; }

    static float QuadraticEaseIn(float p) { return p * p; }
    static float QuadraticEaseOut(float p) { return -(p * (p - 2)); }
    static float QuadraticEaseInOut(float p) { return (p < 0.5f) ? 2 * p * p : (-2 * p * p) + (4 * p) - 1; }

    static float CubicEaseIn(float p) { return p * p * p; }
    static float CubicEaseOut(float p) {
        float f = p - 1;
        return f * f * f + 1;
    }
    static float CubicEaseInOut(float p) {
        if (p < 0.5f)
            return 4 * p * p * p;
        float f = (2 * p) - 2;
        return 0.5f * f * f * f + 1;
    }

    static float QuarticEaseIn(float p) { return p * p * p * p; }
    static float QuarticEaseOut(float p) {
        float f = p - 1;
        return f * f * f * (1 - p) + 1;
    }
    static float QuarticEaseInOut(float p) {
        if (p < 0.5f)
            return 8 * p * p * p * p;
        float f = p - 1;
        return -8 * f * f * f * f + 1;
    }

    static float QuinticEaseIn(float p) { return p * p * p * p * p; }
    static float QuinticEaseOut(float p) {
        float f = p - 1;
        return f * f * f * f * f + 1;
    }
    static float QuinticEaseInOut(float p) {
        if (p < 0.5f)
            return 16 * p * p * p * p * p;
        float f = (2 * p) - 2;
        return 0.5f * f * f * f * f * f + 1;
    }

    static float SineEaseIn(float p) { return std::sin((p - 1) * static_cast<float>(M_PI_2)) + 1; }
    static float SineEaseOut(float p) { return std::sin(p * static_cast<float>(M_PI_2)); }
    static float SineEaseInOut(float p) { return 0.5f * (1 - std::cos(p * static_cast<float>(M_PI))); }

    static float CircularEaseIn(float p) { return 1 - std::sqrt(1 - (p * p)); }
    static float CircularEaseOut(float p) { return std::sqrt((2 - p) * p); }
    static float CircularEaseInOut(float p) {
        if (p < 0.5f)
            return 0.5f * (1 - std::sqrt(1 - 4 * (p * p)));
        return 0.5f * (std::sqrt(-((2 * p) - 3) * ((2 * p) - 1)) + 1);
    }

    static float ExponentialEaseIn(float p) { return (p == 0.0f) ? p : std::pow(2.0f, 10 * (p - 1)); }
    static float ExponentialEaseOut(float p) { return (p == 1.0f) ? p : 1 - std::pow(2.0f, -10 * p); }
    static float ExponentialEaseInOut(float p) {
        if (p == 0.0f || p == 1.0f)
            return p;
        if (p < 0.5f)
            return 0.5f * std::pow(2.0f, (20 * p) - 10);
        return -0.5f * std::pow(2.0f, (-20 * p) + 10) + 1;
    }

    static float ElasticEaseIn(float p) {
        return std::sin(13 * static_cast<float>(M_PI_2) * p) * std::pow(2.0f, 10 * (p - 1));
    }
    static float ElasticEaseOut(float p) {
        return std::sin(-13 * static_cast<float>(M_PI_2) * (p + 1)) * std::pow(2.0f, -10 * p) + 1;
    }
    static float ElasticEaseInOut(float p) {
        if (p < 0.5f)
            return 0.5f * std::sin(13 * static_cast<float>(M_PI_2) * (2 * p)) * std::pow(2.0f, 10 * ((2 * p) - 1));
        return 0.5f *
               (std::sin(-13 * static_cast<float>(M_PI_2) * ((2 * p - 1) + 1)) * std::pow(2.0f, -10 * (2 * p - 1)) + 2);
    }

    static float BackEaseIn(float p) { return p * p * p - p * std::sin(p * static_cast<float>(M_PI)); }
    static float BackEaseOut(float p) {
        float f = 1 - p;
        return 1 - (f * f * f - f * std::sin(f * static_cast<float>(M_PI)));
    }
    static float BackEaseInOut(float p) {
        if (p < 0.5f) {
            float f = 2 * p;
            return 0.5f * (f * f * f - f * std::sin(f * static_cast<float>(M_PI)));
        }
        float f = 1 - (2 * p - 1);
        return 0.5f * (1 - (f * f * f - f * std::sin(f * static_cast<float>(M_PI)))) + 0.5f;
    }

    static float BounceEaseOut(float p) {
        if (p < 4.0f / 11.0f)
            return (121.0f * p * p) / 16.0f;
        if (p < 8.0f / 11.0f)
            return (363.0f / 40.0f * p * p) - (99.0f / 10.0f * p) + 17.0f / 5.0f;
        if (p < 9.0f / 10.0f)
            return (4356.0f / 361.0f * p * p) - (35442.0f / 1805.0f * p) + 16061.0f / 1805.0f;
        return (54.0f / 5.0f * p * p) - (513.0f / 25.0f * p) + 268.0f / 25.0f;
    }
    static float BounceEaseIn(float p) { return 1 - BounceEaseOut(1 - p); }
    static float BounceEaseInOut(float p) {
        if (p < 0.5f)
            return 0.5f * BounceEaseIn(p * 2);
        return 0.5f * BounceEaseOut(p * 2 - 1) + 0.5f;
    }

    static Method getEasingMethod(uint8_t i) {
        switch (i) {
            case kLinearInterpolation:
                return LinearInterpolation;
            case kContinuous:
                return Continuous;
            case kQuadraticEaseIn:
                return QuadraticEaseIn;
            case kQuadraticEaseOut:
                return QuadraticEaseOut;
            case kQuadraticEaseInOut:
                return QuadraticEaseInOut;
            case kCubicEaseIn:
                return CubicEaseIn;
            case kCubicEaseOut:
                return CubicEaseOut;
            case kCubicEaseInOut:
                return CubicEaseInOut;
            case kQuarticEaseIn:
                return QuarticEaseIn;
            case kQuarticEaseOut:
                return QuarticEaseOut;
            case kQuarticEaseInOut:
                return QuarticEaseInOut;
            case kQuinticEaseIn:
                return QuinticEaseIn;
            case kQuinticEaseOut:
                return QuinticEaseOut;
            case kQuinticEaseInOut:
                return QuinticEaseInOut;
            case kSineEaseIn:
                return SineEaseIn;
            case kSineEaseOut:
                return SineEaseOut;
            case kSineEaseInOut:
                return SineEaseInOut;
            case kCircularEaseIn:
                return CircularEaseIn;
            case kCircularEaseOut:
                return CircularEaseOut;
            case kCircularEaseInOut:
                return CircularEaseInOut;
            case kExponentialEaseIn:
                return ExponentialEaseIn;
            case kExponentialEaseOut:
                return ExponentialEaseOut;
            case kExponentialEaseInOut:
                return ExponentialEaseInOut;
            case kElasticEaseIn:
                return ElasticEaseIn;
            case kElasticEaseOut:
                return ElasticEaseOut;
            case kElasticEaseInOut:
                return ElasticEaseInOut;
            case kBackEaseIn:
                return BackEaseIn;
            case kBackEaseOut:
                return BackEaseOut;
            case kBackEaseInOut:
                return BackEaseInOut;
            case kBounceEaseIn:
                return BounceEaseIn;
            case kBounceEaseOut:
                return BounceEaseOut;
            case kBounceEaseInOut:
                return BounceEaseInOut;
            default:
                return nullptr;
        }
    }
};

}  // namespace math
}  // namespace chopper
