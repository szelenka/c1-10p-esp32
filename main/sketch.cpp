// SPDX-License-Identifier: Apache-2.0
// Copyright 2021 Ricardo Quesada
// http://retro.moe/unijoysticle2

#include "sdkconfig.h"

#include <Arduino.h>
#include <Bluepad32.h>
#include <Preferences.h>
#include "include/PinMap.h"
#include "include/SettingsSystem.h"
#include "include/SettingsUser.h"

/*
    Preferences
    https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/preferences.html
*/
Preferences preferences;

/* 
    Dome Position Configuration
*/
#include "chopper/dome/DomePosition.h"
#include "chopper/dome/DomeSensorAnalogPositionProvider.h"
DomeSensorAnalogPositionProvider domeAnalogProvider = DomeSensorAnalogPositionProvider(PIN_DOME_POTENTIOMETER);
DomePosition domeSensor = DomePosition(domeAnalogProvider);

/*
    DimensionEngineering Configuration
*/
#include <SoftwareSerial.h>
#include "chopper/drive/SabertoothDrive.h"

// RX on no pin (unused), TX on pin from PINOUT.h connected to S1 bus
// HardwareSerial sabertoothSerial(3);
// EspSoftwareSerial::UART sabertoothSerial; 

// Setup Sabertooth Driver for Feet
#include "chopper/drive/DifferentialDriveSabertooth.h"
SabertoothDrive sabertoothDiffDrive(SABERTOOTH_TANK_DRIVE_ID, UART_SABERTOOTH, 2);
DifferentialDrive sabertoothDiff(sabertoothDiffDrive.GetMotor(1), sabertoothDiffDrive.GetMotor(2));

// Setup SyRen Driver for Dome
#include "chopper/drive/SingleDriveSabertooth.h"
SabertoothDrive sabertoothSyRenDrive(SABERTOOTH_DOME_DRIVE_ID, UART_SABERTOOTH, 1); 
SingleDrive sabertoothSyRen(sabertoothSyRenDrive.GetMotor(1));

/*
    Maestro Configuration
*/
#include <PololuMaestro.h>

// RX and TX on pin from PINOUT.h connected to opposite TX/RX on Maestro board
// HardwareSerial maestroBodySerial(UART_MAESTRO_BODY);
// HardwareSerial maestroDomeSerial(UART_MAESTRO_DOME);
// EspSoftwareSerial::UART maestroBodySerial;
// EspSoftwareSerial::UART maestroDomeSerial;

// MiniMaestro maestroBody(maestroBodySerial);
// MiniMaestro maestroDome(maestroDomeSerial);

/*
    MP3 Configuration
*/
#include <MP3Trigger.h>

// RX and TX on pin from PINOUT.h connected to opposite TX/RX on MP3 Trigger board
// EspSoftwareSerial::UART mp3TriggerSerial;

MP3Trigger mp3Trigger;

/*
    OpenMV Configuration
*/

// HardwareSerial openMVSerial(UART_OPENMV);
// EspSoftwareSerial::UART openMVSerial;

//
// README FIRST, README FIRST, README FIRST
//
// Bluepad32 has a built-in interactive console.
// By default, it is enabled (hey, this is a great feature!).
// But it is incompatible with Arduino "Serial" class.
//
// Instead of using, "Serial" you can use Bluepad32 "Console" class instead.
// It is somewhat similar to Serial but not exactly the same.
//
// Should you want to still use "Serial", you have to disable the Bluepad32's console
// from "sdkconfig.defaults" with:
//    CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE=n
#include "chopper/core/ControllerDecorator.h"
ControllerDecoratorPtr myControllers[BP32_MAX_GAMEPADS];

void adjustController(ControllerDecoratorPtr ctl)
{
    switch(ctl->getProperties().type) {
            case CONTROLLER_TYPE_SwitchJoyConLeft:
                // type(uint8_t)
                // switch(ctl->getProperties().btaddr) {
                //      5C:52:1E:FF:6E:A2 = JoyCon(L) Pink
                // }
                // axisX is off by -41:-10, axisY is off by 0:14
                ctl->setAxisXOffset(-30);
                // JoyCon orentiation for single-hand position
                ctl->setAxisXInvert(true);
                ctl->setAxisYInvert(true);
                // ctl->setAxisXSlew(1.0, -1.5);
                // ctl->setAxisYSlew(1.0, -1.5);
                break;
            default:
                break;
    }
}

// This callback gets called any time a new gamepad is connected.
// Up to 4 gamepads can be connected at the same time.
void onConnectedController(ControllerPtr ctl) {
    bool foundEmptySlot = false;
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            Console.printf("CALLBACK: Controller is connected, index=%d\n", i);
            // Additionally, you can get certain gamepad properties like:
            // Model, VID, PID, BTAddr, flags, etc.
            ControllerProperties properties = ctl->getProperties();
            Console.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", ctl->getModelName(), properties.vendor_id,
                           properties.product_id);
            myControllers[i] = new ControllerDecorator(ctl);
            adjustController(myControllers[i]);
            foundEmptySlot = true;
            break;
        }
    }
    if (!foundEmptySlot) {
        Console.println("CALLBACK: Controller connected, but could not found empty slot");
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    bool foundController = false;

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] && myControllers[i]->index() == i) {
            Console.printf("CALLBACK: Controller disconnected from index=%d\n", i);
            delete myControllers[i];
            myControllers[i] = nullptr;
            foundController = true;
            break;
        }
    }

    if (!foundController) {
        Console.println("CALLBACK: Controller disconnected, but not found in myControllers");
    }
}

void dumpGamepad(ControllerDecoratorPtr ctl) {
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

void processLeftJoyCon(ControllerDecoratorPtr ctl) {

    if (!ctl->isConnected())
    {
        Console.println("Left JoyCon not connected");
        return;
    }

    // process button inputs
    if (ctl->a()) {
        Console.println("Dpad Left");
        // Turn Periscope Left
    }

    if (ctl->b()) {
        Console.println("Dpad Down");
        // Toggle Body Arm out/in
    }

    if (ctl->x()) {
        Console.println("Dpad Up");
        // Toggle Periscope up/down
    }
    
    if (ctl->y()) {
        Console.println("Dpad Right");
        // Turn Periscope Right
    }

    if (ctl->l1()) {
        Console.println("SL");
    }

    if (ctl->r1()) {
        Console.println("SR");
    }

    if (ctl->l2()) {
        Console.println("L");
        // Raise Head up
    }

    if (ctl->r2()) {
        Console.println("ZL");
        // Spin the Dome to the Left
        
    }

    if (ctl->miscSelect()) {
        Console.println("-");
    }

    if (ctl->miscStart()) {
        Console.println("Screen Capture");
    }

    if (ctl->thumbL()) {
        Console.println("Joystick Push In");
    }

    // Process joystick for drive system
    Console.print(C110P_DRIVE_SYSTEM + " ");
    if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_ARCADE) {
        sabertoothDiff.ArcadeDrive(ctl->axisXslew(), ctl->axisYslew());
    } else if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_CURVE) {
        sabertoothDiff.CurvatureDrive(ctl->axisXslew(), ctl->axisYslew());
    } else if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_TANK) {
        sabertoothDiff.TankDrive(ctl->axisXslew(), ctl->axisYslew());
    } else if (C110P_DRIVE_SYSTEM == C110P_DRIVE_SYSTEM_REELTWO) {
        sabertoothDiff.ReelTwoDrive(ctl->axisXslew(), ctl->axisYslew());
    }
    Console.println("");

}

void processRightJoyCon(ControllerDecoratorPtr ctl) {
    if (!ctl->isConnected())
    {
        Console.println("Right JoyCon not connected");
        return;
    }

    if (ctl->a()) {
        Console.println("A");
    }

    if (ctl->b()) {
        Console.println("X");
    }

    if (ctl->x()) {
        Console.println("B");
    }
    
    if (ctl->y()) {
        Console.println("Y");
    }

    if (ctl->l1()) {
        Console.println("SL");
    }
    
    if (ctl->r1()) {
        Console.println("SR");
    }

    if (ctl->l2()) {
        Console.println("R");
        // Raise Head down
    }

    if (ctl->r2()) {
        Console.println("ZR");
        // Spin the Dome to the Right
    }

    if (ctl->miscSelect()) {
        Console.println("Home");
    }

    if (ctl->miscStart()) {
        Console.println("+");
        // Play a sound
        // TODO: this will cancel any current sound that is playing, to start what is defined here
        // TODO: should we queue the sound instead?
        mp3Trigger.trigger(C110P_SOUND_HELLO);
    }

    if (ctl->thumbL()) {
        Console.println("Joystick Push In");
    }

    //TODO: process joystick for nod system?
}

void processControllers() {
    for (auto myController : myControllers) {
        if (myController && myController->isConnected()) {
            if (myController->hasData()) {
                switch(myController->getProperties().type) {
                    case CONTROLLER_TYPE_SwitchJoyConLeft:
                        processLeftJoyCon(myController);
                        break;
                    case CONTROLLER_TYPE_SwitchJoyConRight:
                        processRightJoyCon(myController);
                        break;
                }
            }
        }
    }
}

void setupBluepad32() {
    Serial.printf("Firmware: %s\n", BP32.firmwareVersion());
    const uint8_t* addr = BP32.localBdAddress();
    Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Setup the Bluepad32 callbacks, and the default behavior for scanning or not.
    // By default, if the "startScanning" parameter is not passed, it will do the "start scanning".
    // Notice that "Start scanning" will try to auto-connect to devices that are compatible with Bluepad32.
    // E.g: if a Gamepad, keyboard or mouse are detected, it will try to auto connect to them.
    bool startScanning = true;
    BP32.setup(&onConnectedController, &onDisconnectedController, startScanning);

    // Notice that scanning can be stopped / started at any time by calling:
    // BP32.enableNewBluetoothConnections(enabled);

    // "forgetBluetoothKeys()" should be called when the user performs
    // a "device factory reset", or similar.
    // Calling "forgetBluetoothKeys" in setup() just as an example.
    // Forgetting Bluetooth keys prevents "paired" gamepads to reconnect.
    // But it might also fix some connection / re-connection issues.
    BP32.forgetBluetoothKeys();

    // Enables mouse / touchpad support for gamepads that support them.
    // When enabled, controllers like DualSense and DualShock4 generate two connected devices:
    // - First one: the gamepad
    // - Second one, which is a "virtual device", is a mouse.
    // By default, it is disabled.
    BP32.enableVirtualDevice(false);

}

void setupSabertooth() {
    // DimensionEngineering setup
    UART_SABERTOOTH_INIT(SABERTOOTH_SERIAL_BAUD_RATE);
    // For HardwareSerial
    // sabertoothSerial.begin(SABERTOOTH_SERIAL_BAUD_RATE, SERIAL_8N1, NOT_A_PIN, PIN_SABERTOOTH_TX);
    // For EspSoftwareSerial
    // sabertoothSerial.begin(SABERTOOTH_SERIAL_BAUD_RATE, SWSERIAL_8N1, NOT_A_PIN, PIN_SABERTOOTH_TX, false);

    // Autobaud is for the whole serial line -- you don't need to do
    // it for each individual motor driver. This is the version of
    // the autobaud command that is not tied to a particular
    // Sabertooth object.
    Sabertooth::autobaud(UART_SABERTOOTH);

    // setTimeout rounds up to the nearest 100 milliseconds
    // A value of 0 disables the serial timeout.
    UART_SABERTOOTH.setTimeout(C110P_MOTOR_TIMEOUT_MS);
    // This setting does not persist between power cycles.
    // See the Packet Serial section of the documentation for what values to use
    // for the minimum voltage command. It may vary between Sabertooth models
    // (2x25, 2x60, etc.).
    //
    // On a Sabertooth 2x25, the value is (Desired Volts - 6) X 5.
    // So, in this sample, we'll make the low battery cutoff 12V: (12 - 6) X 5 = 30.
    // sabertoothDome.setMinVoltage(30);
    // sabertoothDome.setMaxSpeed(100);
    // sabertoothDome.setUseThrottle(false);
    // sabertoothDome.setScaling(false);
    // sabertoothDome.setChannelMixing(false);
    // sabertoothDome.setDomePosition(&domeSensor);

    // sabertoothTankController.setMinVoltage(30);
    sabertoothDiff.SetSpeedLimit(0.65);
    sabertoothDiff.SetSafetyEnabled(true);
    sabertoothDiffDrive.GetMotor(1).SetInverted(true);
    sabertoothDiffDrive.GetMotor(2).SetInverted(false);

    sabertoothSyRen.SetSpeedLimit(0.65);
    sabertoothSyRen.SetSafetyEnabled(true);
    // sabertoothTankController.setUseThrottle(false);
    // sabertoothTankController.setScaling(false);
    // sabertoothTankController.setChannelMixing(true);

    // See the Packet Serial section of the documentation for what values to use
    // for the maximum voltage command. It may vary between Sabertooth models
    // (2x25, 2x60, etc.).
    //
    // On a Sabertooth 2x25, the value is (Desired Volts) X 5.12.
    // In this sample, we'll cap the max voltage before the motor driver does
    // a hard brake at 14V. For a 12V ATX power supply this might be reasonable --
    // at 16V they tend to shut off. Here, if the voltage climbs above
    // 14V due to regenerative braking, the Sabertooth will go into hard brake instead.
    // While this is occuring, the red Error LED will turn on.
    //
    // 14 X 5.12 = 71.68, so we'll go with 71, cutting off slightly below 14V.
    //
    // WARNING: This setting persists between power cycles.
    // ST.setMaxVoltage(71);

    // See the Sabertooth 2x60 documentation for information on ramping values.
    // There are three ranges: 1-10 (Fast), 11-20 (Slow), and 21-80 (Intermediate).
    // The ramping value 14 used here sets a ramp time of 4 seconds for full
    // forward-to-full reverse.
    //
    // 0 turns off ramping. Turning off ramping requires a power cycle.
    //
    // WARNING: The Sabertooth remembers this command between restarts AND in all modes.
    // To change your Sabertooth back to its default, call ST.setRamping(0)
    // ST.setRamping(14);

}

// void debugMaestroPosition(Maestro &maestro) {
//     for (uint8_t i = 0; i < 12; i++)
//     {
//         uint16_t position = maestro.getPosition(i);
//         Serial.print("Channel: ");
//         Serial.print(i);
//         Serial.print(" Position: ");
//         Serial.println(position);
//     }
// }

void setupMaestro() {
    // Set the serial baud rate.
    UART_MAESTRO_BODY_INIT(MAESTRO_SERIAL_BAUD_RATE);
    UART_MAESTRO_DOME_INIT(MAESTRO_SERIAL_BAUD_RATE);
    /* 
        setTarget takes the channel number you want to control, and
        the target position in units of 1/4 microseconds. A typical
        RC hobby servo responds to pulses between 1 ms (4000) and 2
        ms (8000). 
    */
    // Set the target of channel 0 to 1500 us, and wait 2 seconds.
    //   maestro.setTarget(0, 6000);
}

void setupMp3Trigger() {
    mp3Trigger.setup(&UART_MP3TRIGGER);
    UART_MP3TRIGGER_INIT(MP3TRIGGER_SERIAL_BAUD_RATE);
    mp3Trigger.setVolume(C110P_SOUND_VOLUME);
}

void setupOpenMV() {
    UART_OPENMV_INIT(OPENMV_SERIAL_BAUD_RATE);
}

void setupLeds() {
    // setup pins for output
    pinMode(PIN_LED_FRONT, OUTPUT);
    pinMode(PIN_LED_BACK, OUTPUT);

    // ensure LED are off
    digitalWrite(PIN_LED_FRONT, LOW);
    digitalWrite(PIN_LED_BACK, LOW);
}

// Arduino setup function. Runs in CPU 1
void setup() {
    // Set system clock to 0
    // struct timeval raw_time = {};
    // int err = settimeofday(&raw_time, NULL);

    Serial.begin(115200);
    setupBluepad32();
    setupSabertooth();
    setupMaestro();
    setupMp3Trigger();
    setupOpenMV();
    setupLeds();
}

// Arduino loop function. Runs in CPU 1.
void loop() {
    // This call fetches all the controllers' data.
    // Call this function in your main loop.
    bool dataUpdated = BP32.update();
    if (dataUpdated)
    {
        processControllers();
    }
    // Console.printf("Dome Position: %4d\n", domeSensor.getDomePosition());
    
    // We need to update the state of the MP3Trigger each clock cycle
    // ref: https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24
    mp3Trigger.update();

    // The main loop must have some kind of "yield to lower priority task" event.
    // Otherwise, the watchdog will get triggered.
    // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
    // Detailed info here:
    // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time

    //     vTaskDelay(1);
    delay(150);
}
