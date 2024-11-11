#pragma once

// #include <units.h>

#include "chopper/motorController/MotorController.h"
#include "chopper/MotorSafety.h"
#include <Sabertooth.h>

class SabertoothController : virtual public MotorController, 
                             public MotorSafety {
 public:
  SabertoothController(SabertoothController&&) = default;
  SabertoothController& operator=(SabertoothController&&) = default;
  SabertoothController() {};
  SabertoothController(Sabertooth& sabertoothDriver, int motor) :
    m_sabertoothDriver(&sabertoothDriver),
    m_motorId(motor)
  {}
  ~SabertoothController() override = default;
  
  void Set(double speed) override;

  double Get() const override;

  void SetInverted(bool isInverted) override;

  bool GetInverted() const override;

  void Disable() override;

  void StopMotor() override;

  std::string GetDescription() const;

 private:
  Sabertooth* m_sabertoothDriver = nullptr;
  int m_motorId = 0;
};
