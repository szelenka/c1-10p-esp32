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


// Alias for a shared pointer to ControllerDecorator
class Controllers
{
public:

    // Default constructor
    Controllers(DifferentialDrive* sabertoothDiff, SingleDrive* sabertoothSyRen, ExtendedMP3Trigger* mp3Trigger, DomePosition* domeSensor)
        : _sabertoothDiff(sabertoothDiff), _sabertoothSyRen(sabertoothSyRen), _mp3Trigger(mp3Trigger), _domeSensor(domeSensor)
    {
        // Initialize the controller MAC addresses and their roles
        _controllerMacAddresses = std::unordered_map<std::string, ControllerRoles>{
            {CONTROLLER_MAC_ADDRS[0], ControllerRoles::Drive},
            {CONTROLLER_MAC_ADDRS[1], ControllerRoles::Dome},
            {CONTROLLER_MAC_ADDRS[2], ControllerRoles::Animation},
            {CONTROLLER_MAC_ADDRS[3], ControllerRoles::Camera}
        };
        _domeSpinSlewRateLimiter = new SlewRateLimiter(C110P_DOME_SPIN_SLEW_RATE);
    }


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
        if (_ctls[*optRole] != nullptr)
        {
            DEBUG_CONTROLLER_PRINTF("Controller already exists for role: %d\n", *optRole);
            return;
        }
        ControllerProperties properties = ctl->getProperties();
        DEBUG_CONTROLLER_PRINTF("Controller model: %s, VID=0x%04x, PID=0x%04x, index=%d\n", ctl->getModelName(), properties.vendor_id,
                        properties.product_id, *optRole);
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
        if (ctlDrive == nullptr) {
            // DEBUG_CONTROLLER_PRINTLN("Drive controller not found");
            isCtlDriveValid = false;
        } else {
            isCtlDriveValid = ctlDrive->isReady();
        }

        bool isCtlDomeValid = false;
        ControllerDecoratorPtr ctlDome = _ctls[ControllerRoles::Dome];
        if (ctlDome == nullptr) {
            // DEBUG_CONTROLLER_PRINTLN("Dome controller not found");
            isCtlDomeValid = false;
        } else {
            isCtlDomeValid = ctlDome->isReady();
        }

        // process button inputs
        if (isCtlDriveValid && ctlDrive->a()) {
            DEBUG_CONTROLLER_PRINTLN("Dpad Left");
            // Turn Periscope Left
        }

        if (isCtlDriveValid && ctlDrive->b()) {
            DEBUG_CONTROLLER_PRINTLN("Dpad Down");
            // Toggle Body Arm out/in
        }
    
        if (isCtlDriveValid && ctlDrive->x()) {
            DEBUG_CONTROLLER_PRINTLN("Dpad Up");
            // Toggle Periscope up/down
        }
        
        if (isCtlDriveValid && ctlDrive->y()) {
            DEBUG_CONTROLLER_PRINTLN("Dpad Right");
            // Turn Periscope Right
        }
    
        if (isCtlDriveValid && ctlDrive->l1()) {
            DEBUG_CONTROLLER_PRINTLN("SL");
        }
    
        if (isCtlDriveValid && ctlDrive->r1()) {
            DEBUG_CONTROLLER_PRINTLN("SR");
        }
    
        if (isCtlDriveValid && ctlDrive->l2()) {
            DEBUG_CONTROLLER_PRINTLN("L");
            // Raise Head up
        }
    
        // handle dome spin
        if (isCtlDriveValid && isCtlDomeValid)
        {
            processDomeSpin(ctlDrive->r2() > 0, ctlDome->r2() > 0);
        }
            
        // }
        // if (isCtlDriveValid && ctlDrive->r2()) {
        //     DEBUG_CONTROLLER_PRINTLN("ZL");
        // }
    
        if (isCtlDriveValid && ctlDrive->miscSelect()) {
            DEBUG_CONTROLLER_PRINTLN("-");
        }
    
        if (isCtlDriveValid && ctlDrive->miscStart()) {
            DEBUG_CONTROLLER_PRINTLN("Screen Capture");
        }
    
        if (isCtlDriveValid && ctlDrive->thumbL()) {
            DEBUG_CONTROLLER_PRINTLN("Joystick Push In");
        }
    
        if (isCtlDomeValid && ctlDome->a()) {
            DEBUG_CONTROLLER_PRINTLN("A");
            _mp3Trigger->trigger(C110P_SOUND_IMERIALCAROLBELLS);
        }
    
        if (isCtlDomeValid && ctlDome->b()) {
            DEBUG_CONTROLLER_PRINTLN("X");
            _mp3Trigger->trigger(C110P_SOUND_MANDOLORIAN);
        }
    
        if (isCtlDomeValid && ctlDome->x()) {
            DEBUG_CONTROLLER_PRINTLN("B");
        }
        
        if (isCtlDomeValid && ctlDome->y()) {
            DEBUG_CONTROLLER_PRINTLN("Y"); 
        }
    
        if (isCtlDomeValid && ctlDome->l1()) {
            DEBUG_CONTROLLER_PRINTLN("SL");
        }
        
        if (isCtlDomeValid && ctlDome->r1()) {
            DEBUG_CONTROLLER_PRINTLN("SR");
        }
    
        if (isCtlDomeValid && ctlDome->l2()) {
            DEBUG_CONTROLLER_PRINTLN("R");
            // Raise Head down
        }
    
        // if (isCtlDomeValid && ctlDome->r2()) {
        //     DEBUG_CONTROLLER_PRINTLN("ZR");
        // }
    
        if (isCtlDomeValid && ctlDome->miscSelect()) {
            DEBUG_CONTROLLER_PRINTLN("Home");
        }
    
        if (isCtlDomeValid && ctlDome->miscStart()) {
            DEBUG_CONTROLLER_PRINTLN("+");
            _mp3Trigger->triggerRandom();
        }
    
        if (isCtlDomeValid && ctlDome->thumbL()) {
            DEBUG_CONTROLLER_PRINTLN("Joystick Push In");
        }
    
        // Process joystick for drive system
        if (isCtlDriveValid) 
        {
            processDrive(ctlDrive);
        }
    
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
        // TODO: switch on ControllerRoles rather than BP32 role
        switch(role)
        {
            case ControllerRoles::Drive:
                ctl->setAxisXOffset(C110P_CONTROLLER_DRIVE_OFFSET_X);
                ctl->setAxisYOffset(C110P_CONTROLLER_DRIVE_OFFSET_Y);
                ctl->setAxisXInvert(C110P_CONTROLLER_DRIVE_INVERT_X);
                ctl->setAxisYInvert(C110P_CONTROLLER_DRIVE_INVERT_Y);
                ctl->setAxisXSlew(C110P_CONTROLLER_DRIVE_SLEW_RATE_POSITIVE, C110P_CONTROLLER_DRIVE_SLEW_RATE_NEGATIVE);
                ctl->setAxisYSlew(C110P_CONTROLLER_DRIVE_SLEW_RATE_POSITIVE, C110P_CONTROLLER_DRIVE_SLEW_RATE_NEGATIVE);
                break;
            case ControllerRoles::Dome:
                ctl->setAxisXOffset(C110P_CONTROLLER_DOME_OFFSET_X);
                ctl->setAxisYOffset(C110P_CONTROLLER_DOME_OFFSET_Y);
                ctl->setAxisXInvert(C110P_CONTROLLER_DOME_INVERT_X);
                ctl->setAxisYInvert(C110P_CONTROLLER_DOME_INVERT_Y);
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
        ctl->setOutputRange(CONTROLLER_JOYSTICK_MIN_OUTPUT, CONTROLLER_JOYSTICK_MAX_OUTPUT);
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
            _sabertoothSyRen->Drive(_domeSpinSlewRateLimiter->Calculate(C110P_DOME_MOTOR_1_INVERTED ? C110P_DOME_MAXIMUM_SPEED : C110P_DOME_MAXIMUM_SPEED*-1));
        }
        else if (leftPressed && !rightPressed)
        {
            DEBUG_DOME_PRINTLN("Spin the Dome in Positive Direction");
            _sabertoothSyRen->Drive(_domeSpinSlewRateLimiter->Calculate(C110P_DOME_MOTOR_1_INVERTED ? C110P_DOME_MAXIMUM_SPEED*-1 : C110P_DOME_MAXIMUM_SPEED));
        }
        DEBUG_DOME_PRINTF("Dome Position: %4d\n", _domeSensor->getDomePosition());
    }

    // Map to store ControllerRole to MAC Address
    std::unordered_map<std::string, ControllerRoles> _controllerMacAddresses;

    // Unordered map from ControllerRole enum to ControllerDecoratorPtr
    std::unordered_map<ControllerRoles, ControllerDecoratorPtr> _ctls;
    
    DifferentialDrive* _sabertoothDiff = nullptr;
    SingleDrive* _sabertoothSyRen = nullptr;
    ExtendedMP3Trigger* _mp3Trigger = nullptr;
    DomePosition* _domeSensor = nullptr;

    SlewRateLimiter* _domeSpinSlewRateLimiter = nullptr;
};
