#pragma once

#include "chopper/core/ParameterServer.h"
#include "chopper/config/HardwareConfig.h"

namespace chopper {
namespace config {

/**
 * Register all default parameters with the ParameterServer.
 *
 * Call once at application startup, before nodes read their config.
 * Parameters are organized by subsystem with dot-separated names.
 * Returns the number of parameters successfully declared.
 */
inline size_t registerDefaultParameters() {
    auto& ps = core::ParameterServer::getInstance();
    size_t ok = 0;

    // ========================================================================
    // Safety
    // ========================================================================

    ok += ps.declare("safety.motor_enabled",    true,  true);
    ok += ps.declare("safety.motor_timeout_ms", static_cast<int32_t>(500),
                     static_cast<int32_t>(0), static_cast<int32_t>(5000), true);
    ok += ps.declare("safety.serial_timeout_ms", static_cast<int32_t>(450),
                     static_cast<int32_t>(0), static_cast<int32_t>(5000), true);

    // ========================================================================
    // Drive Motor
    // ========================================================================

    ok += ps.declare("drive.ramping_period",  static_cast<int32_t>(80),
                     static_cast<int32_t>(1), static_cast<int32_t>(80));
    ok += ps.declare("drive.max_speed",       0.25f, 0.0f, 1.0f, true);
    ok += ps.declare("drive.speed_boost",     0.15f, 0.0f, 1.0f, true);
    ok += ps.declare("drive.deadband",        0.05f, 0.0f, 0.5f);
    ok += ps.declare("drive.motor1_inverted", true);
    ok += ps.declare("drive.motor2_inverted", false);
    ok += ps.declare("drive.system",          drive_mode::CURVE,
                     drive_mode::ARCADE, drive_mode::REELTWO);

    // ========================================================================
    // Dome Motor
    // ========================================================================

    ok += ps.declare("dome.ramping_period",  static_cast<int32_t>(80),
                     static_cast<int32_t>(1), static_cast<int32_t>(80));
    ok += ps.declare("dome.max_speed",       0.8f, 0.0f, 1.0f, true);
    ok += ps.declare("dome.deadband",        0.05f, 0.0f, 0.5f);
    ok += ps.declare("dome.motor_inverted",  false);
    ok += ps.declare("dome.spin_slew_rate",  2.0f, 0.1f, 20.0f);
    ok += ps.declare("dome.dir_change_thresh",
                     static_cast<int32_t>(5), static_cast<int32_t>(0), static_cast<int32_t>(360));
    ok += ps.declare("dome.random_move_min",
                     static_cast<int32_t>(5), static_cast<int32_t>(0), static_cast<int32_t>(180));

    // ========================================================================
    // Controller — Drive
    // ========================================================================

    ok += ps.declare("ctrl.drive.offset_x",    static_cast<int32_t>(-30),
                     static_cast<int32_t>(-512), static_cast<int32_t>(512));
    ok += ps.declare("ctrl.drive.offset_y",    static_cast<int32_t>(0),
                     static_cast<int32_t>(-512), static_cast<int32_t>(512));
    ok += ps.declare("ctrl.drive.invert_x",    true);
    ok += ps.declare("ctrl.drive.invert_y",    false);
    ok += ps.declare("ctrl.drive.slew_rate",   3.0f, 0.1f, 20.0f);

    // ========================================================================
    // Controller — Dome
    // ========================================================================

    ok += ps.declare("ctrl.dome.offset_x",     static_cast<int32_t>(0),
                     static_cast<int32_t>(-512), static_cast<int32_t>(512));
    ok += ps.declare("ctrl.dome.offset_y",     static_cast<int32_t>(15),
                     static_cast<int32_t>(-512), static_cast<int32_t>(512));
    ok += ps.declare("ctrl.dome.invert_x",     false);
    ok += ps.declare("ctrl.dome.invert_y",     true);
    ok += ps.declare("ctrl.dome.slew_rate",    3.0f, 0.1f, 20.0f);

    // ========================================================================
    // RSS Mechanism (3-RSS Parallel Neck)
    // ========================================================================

    ok += ps.declare("rss.base_altitude",      149.053f, 0.0f, 500.0f);
    ok += ps.declare("rss.effector_altitude",   193.350f, 0.0f, 500.0f);
    ok += ps.declare("rss.bottom_link",        45.0f, 0.0f, 200.0f);
    ok += ps.declare("rss.top_link",           31.0f, 0.0f, 200.0f);
    ok += ps.declare("rss.min_height",         28.621f, 0.0f, 100.0f);
    ok += ps.declare("rss.limit_normal",       0.25f, 0.0f, 1.0f);
    ok += ps.declare("rss.bend_out",           true);
    ok += ps.declare("rss.actuation_range",    static_cast<int32_t>(270),
                     static_cast<int32_t>(180), static_cast<int32_t>(360));
    ok += ps.declare("rss.rotation_offset",    -30.0f, -180.0f, 180.0f);

    // ========================================================================
    // Sound
    // ========================================================================

    ok += ps.declare("sound.volume",           static_cast<int32_t>(0),
                     static_cast<int32_t>(0), static_cast<int32_t>(64));
    ok += ps.declare("sound.default_volume",   static_cast<int32_t>(50),
                     static_cast<int32_t>(0), static_cast<int32_t>(255));

    // ========================================================================
    // Servo PWM — Body Neck A
    // ========================================================================

    ok += ps.declare("servo.neck_a.min",       static_cast<int32_t>(2032),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_a.max",       static_cast<int32_t>(2256),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_a.neutral",   static_cast<int32_t>(2256),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_a.speed",     0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.neck_a.accel",     0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.neck_a.manual",    true);

    // ========================================================================
    // Servo PWM — Body Neck B
    // ========================================================================

    ok += ps.declare("servo.neck_b.min",       static_cast<int32_t>(1952),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_b.max",       static_cast<int32_t>(2176),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_b.neutral",   static_cast<int32_t>(2176),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_b.speed",     0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.neck_b.accel",     0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.neck_b.manual",    true);

    // ========================================================================
    // Servo PWM — Body Neck C
    // ========================================================================

    ok += ps.declare("servo.neck_c.min",       static_cast<int32_t>(2048),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_c.max",       static_cast<int32_t>(2272),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_c.neutral",   static_cast<int32_t>(2272),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.neck_c.speed",     0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.neck_c.accel",     0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.neck_c.manual",    true);

    // ========================================================================
    // Servo PWM — Body Utility Arm
    // ========================================================================

    ok += ps.declare("servo.util_arm.min",     static_cast<int32_t>(1264),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.util_arm.max",     static_cast<int32_t>(2384),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.util_arm.neutral", static_cast<int32_t>(1264),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.util_arm.speed",   0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.util_arm.accel",   0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.util_arm.easing",  static_cast<int32_t>(9),
                     static_cast<int32_t>(0), static_cast<int32_t>(31));

    // ========================================================================
    // Servo PWM — Body Door Right
    // ========================================================================

    ok += ps.declare("servo.bdoor_r.min",      static_cast<int32_t>(992),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.bdoor_r.max",      static_cast<int32_t>(1920),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.bdoor_r.neutral",  static_cast<int32_t>(1920),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.bdoor_r.speed",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.bdoor_r.accel",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.bdoor_r.easing",   static_cast<int32_t>(9),
                     static_cast<int32_t>(0), static_cast<int32_t>(31));

    // ========================================================================
    // Servo PWM — Body Door Left
    // ========================================================================

    ok += ps.declare("servo.bdoor_l.min",      static_cast<int32_t>(1024),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.bdoor_l.max",      static_cast<int32_t>(1696),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.bdoor_l.neutral",  static_cast<int32_t>(1030),
                     static_cast<int32_t>(500), static_cast<int32_t>(2500));
    ok += ps.declare("servo.bdoor_l.speed",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.bdoor_l.accel",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.bdoor_l.easing",   static_cast<int32_t>(9),
                     static_cast<int32_t>(0), static_cast<int32_t>(31));

    // ========================================================================
    // Servo PWM — Dome Periscope Lift
    // ========================================================================

    ok += ps.declare("servo.peri_lift.min",    static_cast<int32_t>(800),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.peri_lift.max",    static_cast<int32_t>(1744),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.peri_lift.neutral", static_cast<int32_t>(800),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.peri_lift.speed",  0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.peri_lift.accel",  0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.peri_lift.easing", static_cast<int32_t>(9),
                     static_cast<int32_t>(0), static_cast<int32_t>(31));

    // ========================================================================
    // Servo PWM — Dome Periscope Spin
    // ========================================================================

    ok += ps.declare("servo.peri_spin.min",    static_cast<int32_t>(496),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.peri_spin.max",    static_cast<int32_t>(2496),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.peri_spin.neutral", static_cast<int32_t>(1282),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.peri_spin.speed",  0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.peri_spin.accel",  0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.peri_spin.easing", static_cast<int32_t>(9),
                     static_cast<int32_t>(0), static_cast<int32_t>(31));

    // ========================================================================
    // Servo PWM — Dome Door Right
    // ========================================================================

    ok += ps.declare("servo.ddoor_r.min",      static_cast<int32_t>(496),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.ddoor_r.max",      static_cast<int32_t>(2304),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.ddoor_r.neutral",  static_cast<int32_t>(2304),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.ddoor_r.speed",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.ddoor_r.accel",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.ddoor_r.easing",   static_cast<int32_t>(9),
                     static_cast<int32_t>(0), static_cast<int32_t>(31));

    // ========================================================================
    // Servo PWM — Dome Door Left
    // ========================================================================

    ok += ps.declare("servo.ddoor_l.min",      static_cast<int32_t>(576),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.ddoor_l.max",      static_cast<int32_t>(2496),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.ddoor_l.neutral",  static_cast<int32_t>(576),
                     static_cast<int32_t>(400), static_cast<int32_t>(2500));
    ok += ps.declare("servo.ddoor_l.speed",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.ddoor_l.accel",    0.0f, 0.0f, 100.0f);
    ok += ps.declare("servo.ddoor_l.easing",   static_cast<int32_t>(9),
                     static_cast<int32_t>(0), static_cast<int32_t>(31));

    return ok;
}

/**
 * Total number of parameters registered by registerDefaultParameters().
 * Useful for verification in tests.
 */
constexpr size_t kExpectedParameterCount = 98;

} // namespace config
} // namespace chopper
