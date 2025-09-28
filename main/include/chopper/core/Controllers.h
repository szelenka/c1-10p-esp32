#pragma once

#include <string>
#include <Bluepad32.h>
#include "include/chopper/core/ControllerDecorator.h"
#include "include/chopper/core/ControllerRoles.h"
#include "include/SettingsSystem.h"
#include "include/SettingsUser.h"
#include "include/SettingsBluetooth.h"

#include "chopper/drive/DifferentialDriveSabertooth.h"
#include "chopper/drive/SingleDriveSabertooth.h"
#include "chopper/sound/ExtendedMP3Trigger.h"
#include "chopper/dome/DomePosition.h"
#include "chopper/filter/SlewRateLimiter.h"
#include "chopper/servo/Dispatch.h"
#include "settings/ServoPinMap.h"
#include "settings/ServoPWM.h"
#include "chopper/servo/RSSMechanism.h"

// Alias for a shared pointer to ControllerDecorator
class Controllers
{
public:

    // Default constructor
    Controllers(DifferentialDrive* sabertoothDiff, SingleDrive* sabertoothSyRen, ExtendedMP3Trigger* mp3Trigger, DomePosition* domeSensor, ServoDispatch* maestroBody, ServoDispatch* maestroDome, RSSMechanism* rssMachine)
        :   _sabertoothDiff(sabertoothDiff), 
            _sabertoothSyRen(sabertoothSyRen),
            _mp3Trigger(mp3Trigger), 
            _domeSensor(domeSensor),
            _maestroBody(maestroBody),
            _maestroDome(maestroDome),
            _rssMachine(rssMachine)
    {
        // Initialize the controller MAC addresses and their roles
        _controllerMacAddresses = std::unordered_map<std::string, ControllerRoles>
        {
            {CONTROLLER_MAC_ADDRS[0], ControllerRoles::Drive},
            {CONTROLLER_MAC_ADDRS[1], ControllerRoles::Dome},
            {CONTROLLER_MAC_ADDRS[2], ControllerRoles::Animation},
            {CONTROLLER_MAC_ADDRS[3], ControllerRoles::Camera}
        };
        _domeSpinSlewRateLimiter = new SlewRateLimiter(C110P_DOME_SPIN_SLEW_RATE);
    };


    // Destructor
    ~Controllers() = default;

    // Method to add a ControllerDecorator if it doesn't already exist
    void addController(ControllerPtr ctl)
    {
        std::string macAddress = ControllerDecorator::formatMacAddress(ctl->getProperties().btaddr);
        std::optional<ControllerRoles> optRole = getRoleFromMacAddress(macAddress);
        if (!optRole.has_value())
        {
            DEBUG_CONTROLLER_PRINTF("Invalid MAC address: %s\n", macAddress.c_str());
            return;
        }
        ctl->setPlayerLEDs(static_cast<int>(*optRole));
        ctl->playDualRumble(10, 1250, 0x80, 0x40);
        if (_ctls[*optRole] != nullptr)
        {
            DEBUG_CONTROLLER_PRINTF("Controller already exists for role: %d\n", optRole);
            return;
        }
        ControllerProperties properties = ctl->getProperties();
        DEBUG_CONTROLLER_PRINTF("Controller model: %s, VID=0x%04x, PID=0x%04x, role=%d\n", 
            ctl->getModelName().c_str(), 
            properties.vendor_id,
            properties.product_id, 
            optRole);
        _ctls[*optRole] = new ControllerDecorator(ctl);
        adjustController(_ctls[*optRole], *optRole);
    }

    // Method to delete a ControllerDecorator by role
    void deleteController(ControllerPtr ctl)
    {
        std::string macAddress = ControllerDecorator::formatMacAddress(ctl->getProperties().btaddr);
        std::optional<ControllerRoles> optRole = getRoleFromMacAddress(macAddress);
        if (!optRole.has_value())
        {
            DEBUG_CONTROLLER_PRINTF("Invalid MAC address: %s\n", macAddress.c_str());
            return;
        }
        auto it = _ctls.find(*optRole);
        if (it != _ctls.end())
        {
            delete it->second; // Free the allocated memory
            _ctls.erase(it); // Remove the entry from the map
            DEBUG_CONTROLLER_PRINTF("Controller of role %d deleted successfully\n", *optRole);
        }
        else
        {
            DEBUG_CONTROLLER_PRINTF("No controller found for role %d\n", *optRole);
        }
    }

    void dumpGamepad(ControllerDecoratorPtr ctl)
    {
        Console.printf(
            "idx=%d, dpad: 0x%02x, buttons: 0x%04x, axis L: %4d, %4d, axis R: %4d, %4d, brake: %4d, throttle: %4d, "
            "misc: 0x%02x, gyro x:%6d y:%6d z:%6d, accel x:%6d y:%6d z:%6d\n",
            ctl->index(),        // Controller Index
            ctl->dpad(),         // D-pad
            ctl->buttons(),      // bitmask of pressed buttons
            ctl->axisX(),        // (-511 - 512) left X Axis
            ctl->axisY(),        // (-511 - 512) left Y axis
            ctl->axisRX(),       // (-511 - 512) right X axis
            ctl->axisRY(),       // (-511 - 512) right Y axis
            ctl->brake(),        // (0 - 1023): brake button
            ctl->throttle(),     // (0 - 1023): throttle (AKA gas) button
            ctl->miscButtons(),  // bitmask of pressed "misc" buttons
            ctl->gyroX(),        // Gyro X
            ctl->gyroY(),        // Gyro Y
            ctl->gyroZ(),        // Gyro Z
            ctl->accelX(),       // Accelerometer X
            ctl->accelY(),       // Accelerometer Y
            ctl->accelZ()        // Accelerometer Z
        );
    }

    void processInputs()
    {
        bool isCtlDriveValid = false;
        ControllerDecoratorPtr ctlDrive = _ctls[ControllerRoles::Drive];
        if (ctlDrive == nullptr)
        {
            // DEBUG_CONTROLLER_PRINTLN("Drive controller not found");
            isCtlDriveValid = false;
        }
        else
        {
            isCtlDriveValid = ctlDrive->isReady();
        }

        bool isCtlDomeValid = false;
        ControllerDecoratorPtr ctlDome = _ctls[ControllerRoles::Dome];
        if (ctlDome == nullptr)
        {
            // DEBUG_CONTROLLER_PRINTLN("Dome controller not found");
            isCtlDomeValid = false;
        }
        else
        {
            isCtlDomeValid = ctlDome->isReady();
        }

        // process button inputs
        if (isCtlDriveValid)
        {
            if (ctlDrive->a().isDoubleClicked())
            {
                DEBUG_CONTROLLER_PRINTLN("Dpad Left -- double click");
                // move full left, without stopping in center
                if (m_periscopeLocation != -1)
                {
                    _maestroDome->setTimedMovement(
                        MAESTRO_DOME_PERISCOPE_SPIN,
                        MAESTRO_DOME_PERISCOPE_SPIN_MIN,
                        MAESTRO_DOME_PERISCOPE_SPIN_MAX,
                        ctlDrive->getButtonState("a").lastPressTime(),
                        800);
                    if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_SPIN))
                    {
                        m_periscopeLocation = -1;
                    }
                }
            }
            else if (ctlDrive->a())
            {
                DEBUG_CONTROLLER_PRINTLN("Dpad Left");
                // Turn Periscope Left
                if (m_periscopeDown)
                {
                    if (m_periscopeLocation == 0)
                    {
                        // facing center, asked to move left
                        _maestroDome->setTimedMovement(
                            MAESTRO_DOME_PERISCOPE_SPIN,
                            MAESTRO_DOME_PERISCOPE_SPIN_NEUTRAL,
                            MAESTRO_DOME_PERISCOPE_SPIN_MAX,
                            ctlDrive->getButtonState("a").lastPressTime(),
                            400);
                        if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_SPIN))
                        {
                            m_periscopeLocation = -1;
                        }
                    }
                    else if (m_periscopeLocation == 1)
                    {
                        // facing right, asked to move left
                        _maestroDome->setTimedMovement(
                            MAESTRO_DOME_PERISCOPE_SPIN,
                            MAESTRO_DOME_PERISCOPE_SPIN_MIN,
                            MAESTRO_DOME_PERISCOPE_SPIN_NEUTRAL,
                            ctlDrive->getButtonState("a").lastPressTime(),
                            400);
                        if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_SPIN))
                        {
                            m_periscopeLocation = 0;
                        }
                    }
                    else if (m_periscopeLocation == -1)
                    {
                        // facing left, asked to move left
                        // nothing to do
                    }
                }
            }
        }

        if (isCtlDriveValid && ctlDrive->b())
        {
            DEBUG_CONTROLLER_PRINTLN("Dpad Down");
            // Toggle Body Arm out/in based on how long the button has been held
            // Move full range defined on Maestro in 800ms
            // TODO: handle press for half the time, release for a quarter, then press again 
            // .. it should start moving where it left off after the release finished
            _maestroBody->setTimedMovement(
                MAESTRO_UTILITY_ARM, 
                MAESTRO_UTILITY_ARM_NEUTRAL, 
                MAESTRO_UTILITY_ARM_MAX, 
                ctlDrive->getButtonState("b").lastPressTime(),
                800
            );
        }
        else if (isCtlDriveValid && !ctlDrive->b())
        {
            // Toggle Body Arm out/in based on how long the button has been held
            // Move full range defined on Maestro in 800ms
            _maestroBody->setTimedMovement(
                MAESTRO_UTILITY_ARM, 
                MAESTRO_UTILITY_ARM_MAX, 
                MAESTRO_UTILITY_ARM_NEUTRAL,
                ctlDrive->getButtonState("b").lastReleaseTime(),
                800);
        }
    
        if (isCtlDriveValid && ctlDrive->x())
        {
            DEBUG_CONTROLLER_PRINTLN("Dpad Up");
            // Toggle Periscope up/down
            if (m_periscopeDown)
            {
                DEBUG_CONTROLLER_PRINTLN("Periscope moving up");
                _maestroDome->setTimedMovement(
                    MAESTRO_DOME_PERISCOPE_LIFT,
                    MAESTRO_DOME_PERISCOPE_LIFT_MIN,
                    MAESTRO_DOME_PERISCOPE_LIFT_MAX,
                    ctlDrive->getButtonState("x").lastPressTime(),
                    800);
                if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_LIFT))
                {
                    m_periscopeDown = false;
                }
            }
            else
            {
                DEBUG_CONTROLLER_PRINTLN("Periscope moving down");
                _maestroDome->setTimedMovement(
                    MAESTRO_DOME_PERISCOPE_LIFT,
                    MAESTRO_DOME_PERISCOPE_LIFT_MAX,
                    MAESTRO_DOME_PERISCOPE_LIFT_MIN,
                    ctlDrive->getButtonState("x").lastPressTime(),
                    800);
                if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_LIFT))
                {
                    m_periscopeDown = true;
                }
            }
        }
        
        
        if (isCtlDriveValid)
        {
            if (ctlDrive->y().isDoubleClicked())
            {
                DEBUG_CONTROLLER_PRINTLN("Dpad Richt -- double click");
                // move full right, without stopping in center
                if (m_periscopeLocation != 1)
                {
                    _maestroDome->setTimedMovement(
                        MAESTRO_DOME_PERISCOPE_SPIN,
                        MAESTRO_DOME_PERISCOPE_SPIN_MAX,
                        MAESTRO_DOME_PERISCOPE_SPIN_MIN,
                        ctlDrive->getButtonState("y").lastPressTime(),
                        800);
                    if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_SPIN))
                    {
                        m_periscopeLocation = 1;
                    }
                }
            }
            else if (ctlDrive->y())
            {
                DEBUG_CONTROLLER_PRINTLN("Dpad Right");
                // Turn Periscope Right
                if (m_periscopeDown)
                {
                    if (m_periscopeLocation == 0)
                    {
                        // facing center, asked to move right
                        _maestroDome->setTimedMovement(
                            MAESTRO_DOME_PERISCOPE_SPIN,
                            MAESTRO_DOME_PERISCOPE_SPIN_NEUTRAL,
                            MAESTRO_DOME_PERISCOPE_SPIN_MIN,
                            ctlDrive->getButtonState("y").lastPressTime(),
                            400);
                        if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_SPIN))
                        {
                            m_periscopeLocation = 1;
                        }
                    }
                    else if (m_periscopeLocation == 1)
                    {
                        // facing right, asked to move right
                        // nothing to do
                    }
                    else if (m_periscopeLocation == -1)
                    {
                        // facing left, asked to move right
                        _maestroDome->setTimedMovement(
                            MAESTRO_DOME_PERISCOPE_SPIN,
                            MAESTRO_DOME_PERISCOPE_SPIN_MAX,
                            MAESTRO_DOME_PERISCOPE_SPIN_NEUTRAL,
                            ctlDrive->getButtonState("y").lastPressTime(),
                            400);
                        if (_maestroDome->isFinishedMoving(MAESTRO_DOME_PERISCOPE_SPIN))
                        {
                            m_periscopeLocation = 0;
                        }
                    }
                }
            }
        }
    
        if (isCtlDriveValid && ctlDrive->l1())
        {
            DEBUG_CONTROLLER_PRINTLN("SL");
            //Volume up one noch/1 sec untill min volume: 30
            if (m_volume <= 64 && m_volume >= 8)  
            {   
                m_volume -= 8;
                mp3Trigger.setVolume(m_volume);
            }
        }
    
        if (isCtlDriveValid && ctlDrive->r1())
        {
            DEBUG_CONTROLLER_PRINTLN("SR");
            //volume down
            if (m_volume >= 0 && m_volume <= 56)
            {
                m_volume += 8;
                mp3Trigger.setVolume(m_volume);
            }
        }
    
        if (isCtlDriveValid && ctlDrive->l2())
        {
            DEBUG_CONTROLLER_PRINTLN("L");
            // body door left
        }
    
        // handle dome spin
        if (isCtlDriveValid && isCtlDomeValid)
        {
            processDomeSpin(ctlDrive->r2(), ctlDome->r2());
        }
        
        // if (isCtlDriveValid && ctlDrive->r2())
        // {
        //     DEBUG_CONTROLLER_PRINTLN("ZL");
        // }
    
        if (isCtlDriveValid && ctlDrive->miscSelect())
        {
            DEBUG_CONTROLLER_PRINTLN("-");
           // Toggle Right Dome Door Open/Closed
            if (m_rightDomeDoorOpen)
            {
                DEBUG_CONTROLLER_PRINTLN("RightDomeDoorOpen");
                _maestroDome->setTimedMovement(
                    MAESTRO_DOME_DOOR_RIGHT,
                    MAESTRO_DOME_DOOR_RIGHT_MAX,
                    MAESTRO_DOME_DOOR_RIGHT_MIN,
                    ctlDrive->getButtonState("miscSelect").lastPressTime(),
                    1);
                m_rightDomeDoorOpen = false;
            }
            else
            {
                DEBUG_CONTROLLER_PRINTLN("RightDoorClosed");
                _maestroDome->setTimedMovement(
                    MAESTRO_DOME_DOOR_RIGHT,
                    MAESTRO_DOME_DOOR_RIGHT_MIN,
                    MAESTRO_DOME_DOOR_RIGHT_MAX,
                    ctlDrive->getButtonState("miscSelect").lastPressTime(),
                    1);
                m_rightDomeDoorOpen = true;
            }
            if (m_leftDomeDoorOpen)
            {
                DEBUG_CONTROLLER_PRINTLN("LeftDomeDoorOpen");
                _maestroDome->setTimedMovement(
                    MAESTRO_DOME_DOOR_LEFT,
                    MAESTRO_DOME_DOOR_LEFT_NEUTRAL,
                    MAESTRO_DOME_DOOR_LEFT_MAX,
                    ctlDrive->getButtonState("miscSelect").lastPressTime(),
                    1);
                m_leftDomeDoorOpen = false;
            }
            else
            {
                DEBUG_CONTROLLER_PRINTLN("LeftDoorClosed");
                _maestroDome->setTimedMovement(
                    MAESTRO_DOME_DOOR_LEFT,
                    MAESTRO_DOME_DOOR_LEFT_MAX,
                    MAESTRO_DOME_DOOR_LEFT_NEUTRAL,
                    ctlDrive->getButtonState("miscSelect").lastPressTime(),
                    1);
                m_leftDomeDoorOpen = true;
            }
        }
    
        if (isCtlDriveValid && ctlDrive->miscStart())
        {
            DEBUG_CONTROLLER_PRINTLN("Screen Capture");
        }
    
        if (isCtlDriveValid && ctlDrive->thumbL().isDoubleClicked() && ctlDrive->thumbL().pressedDuration() < 500)
        {
            DEBUG_CONTROLLER_PRINTLN("Joystick Push In [Drive] -- double click");
            
            if (m_isCarpetMode)
            {
                DEBUG_CONTROLLER_PRINTLN("Decrease Speed");
                _sabertoothDiff->SetSpeedLimit(C110P_DRIVE_MAXIMUM_SPEED);
                _mp3Trigger->trigger(C110P_SOUND_3WAH);
                m_isCarpetMode = false;
            }
            else
            {
                DEBUG_CONTROLLER_PRINTLN("Increase Speed");
                _sabertoothDiff->SetSpeedLimit(C110P_DRIVE_MAXIMUM_SPEED + C110P_DRIVE_SPEED_BOOST);
                _mp3Trigger->trigger(C110P_SOUND_TADA);
                m_isCarpetMode = true;
            }
        }
        // else if (isCtlDriveValid && ctlDrive->thumbL())
        // {
        //     DEBUG_CONTROLLER_PRINTLN("Joystick Push In [Drive]");
        // }
    
        if (isCtlDomeValid && ctlDome->a())
        {
            DEBUG_CONTROLLER_PRINTLN("A");
            _mp3Trigger->trigger(C110P_SOUND_IMERIALCAROLBELLS);
        }
    
        if (isCtlDomeValid && ctlDome->b())
        {
            DEBUG_CONTROLLER_PRINTLN("X");
            _mp3Trigger->trigger(C110P_SOUND_MANDOLORIAN);
        }
    
        if (isCtlDomeValid && ctlDome->x())
        {
            DEBUG_CONTROLLER_PRINTLN("B");
        }
        
        if (isCtlDomeValid && ctlDome->y())
        {
            DEBUG_CONTROLLER_PRINTLN("Y"); 
        }
    
        if (isCtlDomeValid && ctlDome->l1())
        {
            DEBUG_CONTROLLER_PRINTLN("SL");
            _rssMachine->decrementHeight(1);
        }
        
        if (isCtlDomeValid && ctlDome->r1())
        {
            DEBUG_CONTROLLER_PRINTLN("SR");
            _rssMachine->incrementHeight(1);
        }
    
        if (isCtlDomeValid && ctlDome->l2())
        {
            DEBUG_CONTROLLER_PRINTLN("R");
            // body door right
        }
    
        // if (isCtlDomeValid && ctlDome->r2())
        // {
        //     DEBUG_CONTROLLER_PRINTLN("ZR");
        // }
    
        if (isCtlDomeValid && ctlDome->miscSelect())
        {
            DEBUG_CONTROLLER_PRINTLN("Home");
        }
    
        if (isCtlDomeValid && ctlDome->miscStart())
        {
            DEBUG_CONTROLLER_PRINTLN("+");
            _mp3Trigger->triggerRandom();
        }
    
        if (isCtlDomeValid && ctlDome->thumbL().isDoubleClicked())
        {
            DEBUG_CONTROLLER_PRINTLN("Joystick Push In [Dome] -- double click");
            if (_rssMachine->isEnabled())
            {
                _rssMachine->setEnabled(false);
            }
            else
            {
                _rssMachine->setEnabled(true);
            }
        }
    
        // Process joystick for drive system
        if (isCtlDriveValid) 
        {
            processDrive(ctlDrive);
        }

        if (isCtlDomeValid)
        {
            // Process joystick for RSSMachine
            processRSSMachine(ctlDome);
        }
    
        // Process Servo motions all at once
        _maestroBody->animate();
        _maestroDome->animate();

        // We need to update the state of the MP3Trigger each clock cycle
        // ref: https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24
        _mp3Trigger->update();
    }

private:
    // Method to get the controller role from a MAC address
    std::optional<ControllerRoles> getRoleFromMacAddress(const std::string& macAddress) const
    {
        auto it = _controllerMacAddresses.find(macAddress);
        if (it != _controllerMacAddresses.end())
        {
            return it->second;
        }
        return std::nullopt; // Return empty optional if not found
    }

    void adjustController(ControllerDecoratorPtr ctl, ControllerRoles role)
    {
        switch(role)
        {
            case ControllerRoles::Drive:
                ctl->setAxisXOffset(C110P_CONTROLLER_DRIVE_OFFSET_X);
                ctl->setAxisYOffset(C110P_CONTROLLER_DRIVE_OFFSET_Y);
                ctl->setAxisXInvert(C110P_CONTROLLER_DRIVE_INVERT_X);
                ctl->setAxisYInvert(C110P_CONTROLLER_DRIVE_INVERT_Y);
                ctl->setOutputRange(C110P_CONTROLLER_DRIVE_SCALE_MIN, C110P_CONTROLLER_DRIVE_SCALE_MAX);
                ctl->setAxisXSlew(C110P_CONTROLLER_DRIVE_SLEW_RATE_POSITIVE, C110P_CONTROLLER_DRIVE_SLEW_RATE_NEGATIVE);
                ctl->setAxisYSlew(C110P_CONTROLLER_DRIVE_SLEW_RATE_POSITIVE, C110P_CONTROLLER_DRIVE_SLEW_RATE_NEGATIVE);
                break;
            case ControllerRoles::Dome:
                ctl->setAxisXOffset(C110P_CONTROLLER_DOME_OFFSET_X);
                ctl->setAxisYOffset(C110P_CONTROLLER_DOME_OFFSET_Y);
                ctl->setAxisXInvert(C110P_CONTROLLER_DOME_INVERT_X);
                ctl->setAxisYInvert(C110P_CONTROLLER_DOME_INVERT_Y);
                ctl->setOutputRange(C110P_CONTROLLER_DOME_SCALE_MIN, C110P_CONTROLLER_DOME_SCALE_MAX);
                ctl->setAxisXSlew(C110P_CONTROLLER_DOME_SLEW_RATE_POSITIVE, C110P_CONTROLLER_DOME_SLEW_RATE_NEGATIVE);
                ctl->setAxisYSlew(C110P_CONTROLLER_DOME_SLEW_RATE_POSITIVE, C110P_CONTROLLER_DOME_SLEW_RATE_NEGATIVE);
                break;
            case ControllerRoles::Animation:
                break;
            case ControllerRoles::Camera:
                break;
            default:
                Console.printf("Invalid controller role: %d\n", role);
                return;
        }
        ctl->setInputRange(CONTROLLER_JOYSTICK_MIN_INPUT, CONTROLLER_JOYSTICK_MAX_INPUT);
    }

    void processDrive(ControllerDecoratorPtr ctl) 
    {
        DEBUG_DRIVE_PRINTF("%d ", C110P_DRIVE_SYSTEM);
        if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_ARCADE)
        {
            _sabertoothDiff->ArcadeDrive(ctl->axisXslew(), ctl->axisYslew());
        }
        else if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_CURVE)
        {
            _sabertoothDiff->CurvatureDrive(ctl->axisXslew(), ctl->axisYslew());
        }
        else if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_TANK)
        {
            _sabertoothDiff->TankDrive(ctl->axisXslew(), ctl->axisYslew());
        }
        else if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_REELTWO)
        {
            _sabertoothDiff->ReelTwoDrive(ctl->axisXslew(), ctl->axisYslew());
        }
        DEBUG_DRIVE_PRINTLN("");
    }

    void processDomeSpin(bool leftPressed, bool rightPressed)
    {
        if ((leftPressed && rightPressed) || (!leftPressed && !rightPressed))
        {
            // Both controllers are not pressed or both are pressed
            DEBUG_DOME_PRINTLN("Stop Dome");
            // Stop the Dome
            _sabertoothSyRen->Drive(_domeSpinSlewRateLimiter->Calculate(0.0));
        }
        else if (!leftPressed && rightPressed)
        {
            DEBUG_DOME_PRINTLN("Spin the Dome in Negatie Direction");
            _sabertoothSyRen->Drive(_domeSpinSlewRateLimiter->Calculate(C110P_DOME_MOTOR_1_INVERTED ? C110P_DOME_MAXIMUM_SPEED : C110P_DOME_MAXIMUM_SPEED * -1.0f));
        }
        else if (leftPressed && !rightPressed)
        {
            DEBUG_DOME_PRINTLN("Spin the Dome in Positive Direction");
            _sabertoothSyRen->Drive(_domeSpinSlewRateLimiter->Calculate(C110P_DOME_MOTOR_1_INVERTED ? C110P_DOME_MAXIMUM_SPEED * -1.0f : C110P_DOME_MAXIMUM_SPEED));
        }
        DEBUG_DOME_PRINTF("Dome Position: %4d\n", _domeSensor->getDomePosition());
    }

    void processRSSMachine(ControllerDecoratorPtr ctl)
    {
        std::array<uint16_t, 3> legs = _rssMachine->getLegPWMFromJoystick(
            // The oreintation of the JoyCon has the X and Y swapped
            ctl->axisXslew(),
            ctl->axisYslew()
        );

        _maestroBody->setPosition(MAESTRO_BODY_NECK_A, legs[0]);
        _maestroBody->setPosition(MAESTRO_BODY_NECK_B, legs[1]);
        _maestroBody->setPosition(MAESTRO_BODY_NECK_C, legs[2]);
    }

    // Map to store ControllerRole to MAC Address
    std::unordered_map<std::string, ControllerRoles> _controllerMacAddresses;

    // Unordered map from ControllerRole enum to ControllerDecoratorPtr
    std::unordered_map<ControllerRoles, ControllerDecoratorPtr> _ctls;
    
    DifferentialDrive* _sabertoothDiff = nullptr;
    SingleDrive* _sabertoothSyRen = nullptr;
    ExtendedMP3Trigger* _mp3Trigger = nullptr;
    DomePosition* _domeSensor = nullptr;
    ServoDispatch* _maestroBody = nullptr;
    ServoDispatch* _maestroDome = nullptr;
    RSSMechanism* _rssMachine = nullptr;
    SlewRateLimiter* _domeSpinSlewRateLimiter = nullptr;
    bool m_periscopeDown = true;
    bool m_rightDomeDoorOpen = true;
    bool m_leftDomeDoorOpen = true;
    int8_t m_periscopeLocation = 0;
    bool m_isCarpetMode = false;
    int16_t m_volume = 0;
};
