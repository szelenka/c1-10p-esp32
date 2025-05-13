#pragma once

#include "chopper/drive/SingleDrive.h"
#include "chopper/motorController/SabertoothController.h"
#include <Sabertooth.h>
#include <Bluepad32.h>
#include "SettingsSystem.h"

class SingleDriveSabertooth : public SingleDrive
{ 
 public:
  void SetRampingValue(int ramping);
  void SetDeadband(float deadband);
  void SetExpiration(uint64_t expirationTime);
};
