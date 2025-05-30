#include "chopper/motorController/SabertoothController.h"
#include <Bluepad32.h>
#include "SettingsSystem.h"

void SabertoothController::Set(float speed)
{
    float targetSpeed = std::clamp(speed, -1.0f, 1.0f);
    if (GetInverted())
        targetSpeed *= -1;
    int8_t motorSpeed = (int8_t)(targetSpeed * (int8_t)127);
    m_sabertoothDriver->motor(m_motorId, motorSpeed);
    DEBUG_MOTOR_PRINTF("ST[%1d]:%3d ", m_motorId, motorSpeed);
}

float SabertoothController::Get() const
{
    return m_motorId;
}

void SabertoothController::SetInverted(bool isInverted)
{
    m_IsInverted = isInverted;
}

bool SabertoothController::GetInverted() const
{
    return m_IsInverted;
}

void SabertoothController::Disable()
{
    StopMotor();
}

void SabertoothController::StopMotor()
{
    // TODO: do we want to gracefully slow down the motors when encountering an explicit Stop?
    Set(0.0f);
}

std::string SabertoothController::GetDescription() const
{
     return "Sabertooth" + m_motorId;//fmt::format("Sabertooth {}", m_motorId);
}
