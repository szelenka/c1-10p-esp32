#pragma once

#include "chopper/core/Node.h"
#include "chopper/core/Executor.h"
#include "chopper/safety/SafetyManager.h"
#include "chopper/telemetry/TelemetryService.h"

namespace chopper::nodes {

class TelemetryNode : public core::Node {
public:
    TelemetryNode(core::Executor* executor, safety::SafetyManager* safety, telemetry::TelemetryService* service,
                  double hz = 4.0)
        : Node("telemetry_node"), executor_(executor), safety_(safety), service_(service), hz_(hz) {
        // JSON formatting / stats fetch can exceed the default 1ms node budget.
        setMaxExecutionTime(20000);
    }

    bool initialize() override { return (executor_ != nullptr) && (safety_ != nullptr) && (service_ != nullptr); }

    void process(uint64_t now) override {
        core::Executor::Statistics stats = executor_->getStatistics();

        telemetry::TelemetryService::Snapshot snap{};
        snap.timestamp_us = now;
        snap.loop_count = stats.loop_count;
        snap.max_loop_time_us = stats.max_loop_time_us;
        snap.avg_loop_time_us = stats.avg_loop_time_us;
        snap.active_nodes = stats.active_nodes;
        snap.total_nodes = stats.total_nodes;
        snap.executor_estop = executor_->isEmergencyStop();
        snap.safety_estop = safety_->isEmergencyStopped();
        snap.degradation_mode = static_cast<uint8_t>(safety_->getDegradationManager().getCurrentMode());

        service_->update(snap);
    }

    void emergencyStop() override {
        if (service_ == nullptr) {
            return;
        }

        telemetry::TelemetryService::Snapshot snap{};
        snap.executor_estop = true;
        snap.safety_estop = true;
        service_->update(snap);
    }

    [[nodiscard]] double getUpdateFrequency() const override { return hz_; }

private:
    core::Executor* executor_;
    safety::SafetyManager* safety_;
    telemetry::TelemetryService* service_;
    double hz_;
};

}  // namespace chopper::nodes
