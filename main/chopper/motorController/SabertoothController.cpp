#include "chopper/motorController/SabertoothController.h"

void SabertoothController::Set(double speed) {
    double targetSpeed = std::clamp(speed, -1.0, 1.0);
    m_sabertoothDriver->motor(m_motorId, targetSpeed * 127);
}

double SabertoothController::Get() const {
    return m_motorId;
}

void SabertoothController::SetInverted(bool isInverted) {
    m_IsInverted = isInverted;
}

bool SabertoothController::GetInverted() const {
    return m_IsInverted;
}

void SabertoothController::Disable() {
    StopMotor();
}

void SabertoothController::StopMotor() {
    Set(0.0);
}

std::string SabertoothController::GetDescription() const {
     return "Sabertooth" + m_motorId;//fmt::format("Sabertooth {}", m_motorId);
}
