#pragma once

// #include <units.h>

#include "chopper/motorController/MotorController.h"
#include "chopper/MotorSafety.h"
#include <Sabertooth.h>
#include <Bluepad32.h>

class SabertoothController : virtual public MotorController
{
 public:
  // SabertoothController(SabertoothController&&) = default;
  // SabertoothController& operator=(SabertoothController&&) = default;
  // SabertoothController() {};
  SabertoothController(Sabertooth& sabertoothDriver, uint8_t motor) :
    m_sabertoothDriver(&sabertoothDriver),
    m_motorId(motor)
  {
    Console.println("SabertoothController [init]");
  }
  // ~SabertoothController() override = default;
  
  void Set(double speed) override;

  double Get() const override;

  void SetInverted(bool isInverted) override;

  bool GetInverted() const override;

  void Disable() override;

  void StopMotor() override;

  std::string GetDescription() const;

 private:
  Sabertooth* m_sabertoothDriver = nullptr;
  uint8_t m_motorId = 0;
};
