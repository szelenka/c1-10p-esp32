#ifndef TankDrive_h
#define TankDrive_h

#include <Arduino.h>
#include "SettingsSystem.h"

#ifdef USE_MOTOR_DEBUG
#define MOTOR_DEBUG_PRINT(s) DEBUG_PRINT(s)
#define MOTOR_DEBUG_PRINTLN(s) DEBUG_PRINTLN(s)
#define MOTOR_DEBUG_PRINT_HEX(s) DEBUG_PRINT_HEX(s)
#define MOTOR_DEBUG_PRINTLN_HEX(s) DEBUG_PRINTLN_HEX(s)
#else
#define MOTOR_DEBUG_PRINT(s)
#define MOTOR_DEBUG_PRINTLN(s)
#define MOTOR_DEBUG_PRINT_HEX(s)
#define MOTOR_DEBUG_PRINTLN_HEX(s)
#endif

struct motor_t {
    float left;
    float right;
    float throttle;
};

class TankDrive {
public:
    TankDrive();

    virtual void setup()
    {
    }

    bool getEnable();
    void setEnable(bool enable);

    int getSerialLatency();
    void setSerialLatency(int ms);

    bool getChannelMixing();
    void setChannelMixing(bool mixing);

    bool getThrottleInverted();
    void setThrottleInverted(bool invert);

    bool getTurnInverted();
    void setTurnInverted(bool invert);

    bool getScaling();
    void setScaling(bool scaling);

    float getMaxSpeed();
    void setMaxSpeed(float modifier);

    unsigned getThrottleAccelerationScale();
    void setThrottleAccelerationScale(unsigned scale);

    unsigned getThrottleDecelerationScale();
    void setThrottleDecelerationScale(unsigned scale);

    unsigned getTurnAccelerationScale();
    void setTurnAccelerationScale(unsigned scale);

    unsigned getTurnDecelerationScale();
    void setTurnDecelerationScale(unsigned scale);

    void setAccelerationScale(unsigned scale);
    void setDecelerationScale(unsigned scale);

    // void setTargetSteering(TargetSteering* target);

    bool useThrottle();
    bool useHardStop();

    void setUseThrottle(bool use);
    void setUseHardStop(bool use);

    virtual void stop()
    {
        MOTOR_DEBUG_PRINTLN("[TankDrive:stop] STOP!");
        fMotorsStopped = true;
        fDriveThrottle = 0;
        fDriveTurning = 0;
    }
    bool motorStopped();

    virtual void animate(int xAxis, int yAxis, int speedModifier);

protected:
    virtual void motor(float left, float right, float throttle) = 0;

    virtual float getThrottle();
    virtual bool hasThrottle();
    virtual float throttleSpeed(float speedModifier);

    void motionVector(int xAxis, int yAxis, int speedModifier);

protected:
    // ControllerPtr &fControllers;
    // TargetSteering* fTargetSteering;
    bool fEnabled = false;
    bool fWasConnected = false;
    bool fMotorsStopped = false;
    bool fChannelMixing = false;
    bool fScaling = false;
    bool fUseThrottle = false;
    bool fUseHardStop = true;
    bool fThrottleInverted = false;
    bool fTurnInverted = false;
    float fSpeedModifier = 0;
    int fSerialLatency = 0;
    int fLastCommand = 0;
    unsigned fThrottleAccelerationScale = 0;
    unsigned fThrottleDecelerationScale = 0;
    unsigned fTurnAccelerationScale = 0;
    unsigned fTurnDecelerationScale = 0;
    float fDriveThrottle = 0;
    float fDriveTurning = 0;
};

#endif
