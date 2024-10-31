#include "TankDrive.h"

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


TankDrive::TankDrive()
{
    // Default enabled
    setEnable(true);
    // Default to half speed max
    setMaxSpeed(0.5);
    // Default 25ms
    setSerialLatency(25);
    setScaling(true);
    setThrottleAccelerationScale(100);
    setThrottleDecelerationScale(100);
    setTurnAccelerationScale(200);
    setTurnDecelerationScale(20);
    setChannelMixing(true);
    setThrottleInverted(false);
    setTurnInverted(false);
    setUseThrottle(false);
    setUseHardStop(true);
}


bool TankDrive::getEnable()
{
    return fEnabled;
}

void TankDrive::setEnable(bool enable)
{
    fEnabled = enable;
}

int TankDrive::getSerialLatency()
{
    return fSerialLatency;
}

void TankDrive::setSerialLatency(int ms)
{
    fSerialLatency = ms;
}

bool TankDrive::getChannelMixing()
{
    return fChannelMixing;
}

void TankDrive::setChannelMixing(bool mixing)
{
    fChannelMixing = mixing;
}

bool TankDrive::getThrottleInverted()
{
    return fThrottleInverted;
}

void TankDrive::setThrottleInverted(bool invert)
{
    fThrottleInverted = invert;
}

bool TankDrive::getTurnInverted()
{
    return fTurnInverted;
}

void TankDrive::setTurnInverted(bool invert)
{
    fTurnInverted = invert;
}

bool TankDrive::getScaling()
{
    return fScaling;
}

void TankDrive::setScaling(bool scaling)
{
    fScaling = scaling;
}

float TankDrive::getMaxSpeed()
{
    return fSpeedModifier;
}

void TankDrive::setMaxSpeed(float modifier)
{
    stop();
    fSpeedModifier = min(max(modifier, 0.0f), 1.0f);
}

unsigned TankDrive::getThrottleAccelerationScale()
{
    return fThrottleAccelerationScale;
}

void TankDrive::setThrottleAccelerationScale(unsigned scale)
{
    fThrottleAccelerationScale = scale;
}

unsigned TankDrive::getThrottleDecelerationScale()
{
    return fThrottleDecelerationScale;
}

void TankDrive::setThrottleDecelerationScale(unsigned scale)
{
    fThrottleDecelerationScale = scale;
}

unsigned TankDrive::getTurnAccelerationScale()
{
    return fTurnAccelerationScale;
}

void TankDrive::setTurnAccelerationScale(unsigned scale)
{
    fTurnAccelerationScale = scale;
}

unsigned TankDrive::getTurnDecelerationScale()
{
    return fTurnDecelerationScale;
}

void TankDrive::setTurnDecelerationScale(unsigned scale)
{
    fTurnDecelerationScale = scale;
}

void TankDrive::setAccelerationScale(unsigned scale)
{
    setThrottleAccelerationScale(scale);
    setTurnAccelerationScale(scale);
}

void TankDrive::setDecelerationScale(unsigned scale)
{
    setThrottleDecelerationScale(scale);
    setTurnDecelerationScale(scale);
}

// void setTargetSteering(TargetSteering* target)
// {
//     fTargetSteering = target;
// }

bool TankDrive::useThrottle()
{
    return fUseThrottle;
}

bool TankDrive::useHardStop()
{
    return fUseHardStop;
}

void TankDrive::setUseThrottle(bool use)
{
    fUseThrottle = use;
}

void TankDrive::setUseHardStop(bool use)
{
    fUseHardStop = use;
}

bool TankDrive::motorStopped()
{
    return fMotorsStopped;
}

/**
     * Dispatch any received i2c event to CommandEvent
     */
void TankDrive::animate(int xAxis, int yAxis, int speedModifier)
{
    if (!fEnabled)
    {
        stop();
        if (!fMotorsStopped)
        {
            MOTOR_DEBUG_PRINTLN("STOP");
            stop();
        }
        /* Disable Target Steering */
        // setTargetSteering(nullptr);
    }
    else
    {
        motionVector(xAxis, yAxis, speedModifier);
    }
}

float TankDrive::getThrottle()
{
    // if (useThrottle() && fDriveStick.isConnected())
    // {
    //     if (useLeftStick())
    //         return(fControllers[LEFT]->throttle())/255.0f;
    //         // return float(fDriveStick.state.analog.button.l2)/255.0f;
    //     if (useRightStick())
    //         return(fControllers[RIGHT]->throttle())/255.0f;
    //         // return float(fDriveStick.state.analog.button.r2)/255.0f;
    // }
    return 0.0f;
}

bool TankDrive::hasThrottle()
{
    return false;
}

float TankDrive::throttleSpeed(float speedModifier)
{
    // if (useThrottle())
    // {
    //     if (!hasThrottle())
    //     {
    //         // We are going to simulate the throttle by increasing the speed modifier
    //         if (fDriveStick.isConnected())
    //         {
    //             speedModifier += getThrottle() * ((1.0f-speedModifier));
    //         }
    //         return min(max(speedModifier,0.0f),1.0f) * -1.0f;
    //     }
    //     else
    //     {
    //         if (fDriveStick.isConnected())
    //         {
    //             speedModifier = getThrottle();
    //         }
    //     }
    // }
    return speedModifier;
}

void TankDrive::motionVector(int xAxis, int yAxis, int speedModifier)
{
    // DEBUG_PRINTF(
    //     "[TanksDrive::motionVector] axis X: %4d, axis Y: %4d, speedModifier: %4d\n",
    //     xAxis, yAxis, speedModifier
    // );
    int axisScale = 512;
    fWasConnected = true;
    if (millis() - fLastCommand > fSerialLatency)
    {
        // float drive_mod = speedModifier * -1.0f;
        auto drive_mod = throttleSpeed(speedModifier);
        auto turning = (float)(xAxis + axisScale) / (axisScale - 0.5f) - 1.0f;
        auto throttle = (float)(yAxis + axisScale) / (axisScale - 0.5f) - 1.0f;
        if (fThrottleInverted)
            throttle = -throttle;
        if (fTurnInverted)
            turning = -turning;

        if (abs(turning) < 0.2)
            turning = 0;
        else
            turning = pow(abs(turning)-0.2, 1.4) * ((turning < 0) ? -1 : 1);
        if (abs(throttle) < 0.2)
            throttle = 0;

        // if (fTargetSteering)
        // {
        //     auto targetThrottle = fTargetSteering->getThrottle();
        //     auto targetTurning = fTargetSteering->getTurning();
        //     throttle = (throttle != 0) ? throttle : targetThrottle;
        //     turning = (turning != 0) ? turning : targetTurning;
        // }

        // clamp turning if throttle is greater than turning
        // theory being that if you are at top speed turning should
        // be less responsive. if you are full on turning throttle
        // should remain responsive.
        // if (abs(turning) <= abs(fDriveThrottle))
        // {
        //     if (abs(fDriveThrottle) >= 0.8)
        //         turning /= 8;
        //     else if (abs(fDriveThrottle) >= 0.6)
        //         turning /= 6;
        //     else if (abs(fDriveThrottle) >= 0.4)
        //         turning /= 4;
        //     else if (abs(fDriveThrottle) >= 0.2)
        //         turning /= 2;
        // }
        if (turning != 0 || throttle != 0)
        {
            DEBUG_PRINTF(
                "[TanksDrive::motionVector] axis X: %4d, axis Y: %4d, turning: %2.6f, throttle: %2.6f\n",
                xAxis, yAxis, turning, throttle
            );
        }
        if (fScaling)
        {
            if (throttle > fDriveThrottle)
            {
                float scale = fThrottleAccelerationScale;
                if (fDriveThrottle < 0)
                {
                    MOTOR_DEBUG_PRINT("DECELERATING ");
                    scale = fThrottleDecelerationScale;
                }
                else
                {
                    MOTOR_DEBUG_PRINT("ACCELERATING REVERSE ");
                }
                float val = max(abs(throttle - fDriveThrottle) / scale, 0.01f);
                MOTOR_DEBUG_PRINT(val);
                MOTOR_DEBUG_PRINT(" throttle: ");
                MOTOR_DEBUG_PRINT(throttle);
                MOTOR_DEBUG_PRINT(" drive : ");
                MOTOR_DEBUG_PRINT(fDriveThrottle );
                MOTOR_DEBUG_PRINT(" => ");
                fDriveThrottle = ((int)round(min(fDriveThrottle + val, throttle)*100))/100.0f;
                MOTOR_DEBUG_PRINTLN(fDriveThrottle );
            }
            else if (throttle < fDriveThrottle)
            {
                float scale = fThrottleAccelerationScale;
                if (fDriveThrottle > 0)
                {
                    MOTOR_DEBUG_PRINT("DECELERATING REVERSE ");
                    scale = fThrottleDecelerationScale;
                }
                else
                {
                    MOTOR_DEBUG_PRINT("ACCELERATING ");
                }
                float val = abs(throttle - fDriveThrottle) / scale;
                MOTOR_DEBUG_PRINT(val);
                MOTOR_DEBUG_PRINT(" throttle: ");
                MOTOR_DEBUG_PRINT(throttle);
                MOTOR_DEBUG_PRINT(" drive : ");
                MOTOR_DEBUG_PRINT(fDriveThrottle );
                MOTOR_DEBUG_PRINT(" => ");
                fDriveThrottle = ((int)floor(max(fDriveThrottle - val, throttle)*100))/100.0f;
                MOTOR_DEBUG_PRINTLN(fDriveThrottle );
            }
            // Scale turning by fDriveThrottle
            turning *= (1.0f - abs(fDriveThrottle) * 0.1);
            if (turning > fDriveTurning)
            {
                float scale = fTurnAccelerationScale;
                if (fDriveTurning < 0)
                {
                    MOTOR_DEBUG_PRINT("DECELERATING LEFT ");
                    scale = fTurnDecelerationScale;
                }
                else
                {
                    MOTOR_DEBUG_PRINT("ACCELERATING RIGHT ");
                }
                float val = max(abs(turning - fDriveTurning) / scale, 0.01f);
                MOTOR_DEBUG_PRINT(val);
                MOTOR_DEBUG_PRINT(" turning: ");
                MOTOR_DEBUG_PRINT(turning);
                MOTOR_DEBUG_PRINT(" drive : ");
                MOTOR_DEBUG_PRINT(fDriveTurning );
                MOTOR_DEBUG_PRINT(" => ");
                fDriveTurning = ((int)round(min(fDriveTurning + val, turning)*100))/100.0f;
                MOTOR_DEBUG_PRINTLN(fDriveTurning );
                MOTOR_DEBUG_PRINT(" ");
            }
            else if (turning < fDriveTurning)
            {
                float scale = fTurnAccelerationScale;
                if (fDriveTurning > 0)
                {
                    MOTOR_DEBUG_PRINT("DECELERATING RIGHT ");
                    scale = fTurnDecelerationScale;
                }
                else
                {
                    MOTOR_DEBUG_PRINT("ACCELERATING LEFT ");
                }
                float val = abs(turning - fDriveTurning) / scale;
                MOTOR_DEBUG_PRINT(val);
                MOTOR_DEBUG_PRINT(" turning: ");
                MOTOR_DEBUG_PRINT(turning);
                MOTOR_DEBUG_PRINT(" drive : ");
                MOTOR_DEBUG_PRINT(fDriveTurning );
                MOTOR_DEBUG_PRINT(" => ");
                fDriveTurning = ((int)floor(max(fDriveTurning - val, turning)*100))/100.0f;
                MOTOR_DEBUG_PRINTLN(fDriveTurning );
            }
        }
        else
        {
            fDriveThrottle = throttle;
            fDriveTurning = turning;
        }

        auto x = fDriveThrottle;
        auto y = fDriveTurning;

        auto target_left = x;
        auto target_right = y;
        if (fChannelMixing)
        {
            auto r = hypot(x, y);
            auto t = atan2(y, x);
            // rotate 45 degrees
            t += M_PI / 4;

            target_left = r * cos(t);
            target_right = r * sin(t);

            // rescale the new coords
            target_left *= sqrt(2);
            target_right *= sqrt(2);
        }
        // clamp to -1/+1 and apply max speed limit
        target_left = max(-1.0f, min(target_left, 1.0f));
        target_right = max(-1.0f, min(target_right, 1.0f));
        DEBUG_PRINTF(
            "target_left: %2.6f, target_right: %2.6f throttle: %2.6f\n", 
            target_left, target_right, drive_mod);

        motor(target_left, target_right, drive_mod);
        fLastCommand = millis();
        fMotorsStopped = false;
        // struct motor_t motor = {target_left, target_right, drive_mod};
        // return motor;
    }
}