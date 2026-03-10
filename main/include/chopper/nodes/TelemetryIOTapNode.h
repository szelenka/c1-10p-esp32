#pragma once

#include "chopper/core/PublishingNode.h"
#include "chopper/messages/CommonMessages.h"
#include "chopper/telemetry/TelemetryService.h"

namespace chopper {
namespace nodes {

class TelemetryIOTapNode : public core::PublishingNode {
public:
    explicit TelemetryIOTapNode(telemetry::TelemetryService* service)
        : PublishingNode("telemetry_io_tap")
        , service_(service)
    {
        // Subscriptions should stay lightweight and never trigger node-timeout E-stop.
        setMaxExecutionTime(5000);
    }

    bool initialize() override {
        if (!service_) {
            return false;
        }

        drive_in_sub_ = createSubscription<messages::ControllerInput>(
            "controller/drive", &TelemetryIOTapNode::onDriveInput, this);
        dome_in_sub_ = createSubscription<messages::ControllerInput>(
            "controller/dome", &TelemetryIOTapNode::onDomeInput, this);
        animation_in_sub_ = createSubscription<messages::ControllerInput>(
            "controller/animation", &TelemetryIOTapNode::onAnimationInput, this);
        camera_in_sub_ = createSubscription<messages::ControllerInput>(
            "controller/camera", &TelemetryIOTapNode::onCameraInput, this);

        motor_out_sub_ = createSubscription<messages::MotorCommand>(
            "drive/cmd", &TelemetryIOTapNode::onMotorCommand, this);
        dome_motor_out_sub_ = createSubscription<messages::MotorCommand>(
            "dome/motor/cmd", &TelemetryIOTapNode::onMotorCommand, this);
        servo_out_sub_ = createSubscription<messages::ServoCommand>(
            "servo/cmd", &TelemetryIOTapNode::onServoAnyCommand, this);
        servo_body_out_sub_ = createSubscription<messages::ServoCommand>(
            "servo/body/cmd", &TelemetryIOTapNode::onServoBodyCommand, this);
        servo_dome_out_sub_ = createSubscription<messages::ServoCommand>(
            "servo/dome/cmd", &TelemetryIOTapNode::onServoDomeCommand, this);
        led_out_sub_ = createSubscription<messages::LEDCommand>(
            "led/cmd", &TelemetryIOTapNode::onLedCommand, this);
        led_front_out_sub_ = createSubscription<messages::LEDCommand>(
            "led/front/cmd", &TelemetryIOTapNode::onLedCommand, this);
        led_back_out_sub_ = createSubscription<messages::LEDCommand>(
            "led/back/cmd", &TelemetryIOTapNode::onLedCommand, this);
        audio_out_sub_ = createSubscription<messages::AudioCommand>(
            "audio/cmd", &TelemetryIOTapNode::onAudioCommand, this);
        status_sub_ = createSubscription<messages::SystemStatus>(
            "system/status", &TelemetryIOTapNode::onSystemStatus, this);

        return drive_in_sub_ && dome_in_sub_ && animation_in_sub_ && camera_in_sub_ &&
               motor_out_sub_ && dome_motor_out_sub_ &&
               servo_out_sub_ && servo_body_out_sub_ && servo_dome_out_sub_ &&
               led_out_sub_ && led_front_out_sub_ && led_back_out_sub_ &&
               audio_out_sub_ && status_sub_;
    }

    void process(uint64_t) override {
        // Event-driven only: callbacks update telemetry service.
    }

    void emergencyStop() override {
        // No additional action needed; data will reflect latest observed state.
    }

    double getUpdateFrequency() const override { return 0.0; }

private:
    static void onDriveInput(const messages::ControllerInput& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeInput(telemetry::TelemetryService::InputRole::DRIVE, msg);
    }

    static void onDomeInput(const messages::ControllerInput& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeInput(telemetry::TelemetryService::InputRole::DOME, msg);
    }

    static void onAnimationInput(const messages::ControllerInput& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeInput(telemetry::TelemetryService::InputRole::ANIMATION, msg);
    }

    static void onCameraInput(const messages::ControllerInput& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeInput(telemetry::TelemetryService::InputRole::CAMERA, msg);
    }

    static void onMotorCommand(const messages::MotorCommand& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeMotorCommand(msg);
    }

    static void onServoAnyCommand(const messages::ServoCommand& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeServoCommand(
            msg, telemetry::TelemetryService::ServoSourceGroup::ANY);
    }

    static void onServoBodyCommand(const messages::ServoCommand& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeServoCommand(
            msg, telemetry::TelemetryService::ServoSourceGroup::BODY);
    }

    static void onServoDomeCommand(const messages::ServoCommand& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeServoCommand(
            msg, telemetry::TelemetryService::ServoSourceGroup::DOME);
    }

    static void onLedCommand(const messages::LEDCommand& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeLedCommand(msg);
    }

    static void onAudioCommand(const messages::AudioCommand& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeAudioCommand(msg);
    }

    static void onSystemStatus(const messages::SystemStatus& msg, void* ctx) {
        auto* self = static_cast<TelemetryIOTapNode*>(ctx);
        if (!self || !self->service_) return;
        self->service_->observeSystemStatus(msg);
    }

    telemetry::TelemetryService* service_;

    core::TypedSubscriptionPtr<messages::ControllerInput> drive_in_sub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> dome_in_sub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> animation_in_sub_;
    core::TypedSubscriptionPtr<messages::ControllerInput> camera_in_sub_;
    core::TypedSubscriptionPtr<messages::MotorCommand> motor_out_sub_;
    core::TypedSubscriptionPtr<messages::MotorCommand> dome_motor_out_sub_;
    core::TypedSubscriptionPtr<messages::ServoCommand> servo_out_sub_;
    core::TypedSubscriptionPtr<messages::ServoCommand> servo_body_out_sub_;
    core::TypedSubscriptionPtr<messages::ServoCommand> servo_dome_out_sub_;
    core::TypedSubscriptionPtr<messages::LEDCommand> led_out_sub_;
    core::TypedSubscriptionPtr<messages::LEDCommand> led_front_out_sub_;
    core::TypedSubscriptionPtr<messages::LEDCommand> led_back_out_sub_;
    core::TypedSubscriptionPtr<messages::AudioCommand> audio_out_sub_;
    core::TypedSubscriptionPtr<messages::SystemStatus> status_sub_;
};

} // namespace nodes
} // namespace chopper
