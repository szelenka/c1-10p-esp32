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
#include "include/SettingsBluetooth.h"

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
#include "chopper/servo/Dispatch.h"

// RX and TX on pin from PINOUT.h connected to opposite TX/RX on Maestro board
// ref: https://www.pololu.com/docs/0J40/5.g
ServoDispatch maestroBody(UART_MAESTRO_BODY, Maestro::noResetPin, MAESTRO_BODY_ID, false);
ServoDispatch maestroDome(UART_MAESTRO_DOME, Maestro::noResetPin, MAESTRO_DOME_ID, false);

/*
    MP3 Configuration
*/
#include "chopper/sound/ExtendedMP3Trigger.h"
ExtendedMP3Trigger mp3Trigger;

/*
    OpenMV Configuration
*/

// HardwareSerial openMVSerial(UART_OPENMV);
// EspSoftwareSerial::UART openMVSerial;

#include "chopper/core/Controllers.h"
Controllers myControllers(
    &sabertoothDiff, 
    &sabertoothSyRen, 
    &mp3Trigger, 
    &domeSensor,
    &maestroBody,
    &maestroDome
);

// This callback gets called any time a new gamepad is connected.
// Up to 4 gamepads can be connected at the same time.
void onConnectedController(ControllerPtr ctl) {
    myControllers.addController(ctl);
}

void onDisconnectedController(ControllerPtr ctl) {
    myControllers.deleteController(ctl);
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
    if (CONTROLLER_FORGET_MAC_ADDR) {
        BP32.forgetBluetoothKeys();
    }

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

    // Autobaud is for the whole serial line -- you don't need to do
    // it for each individual motor driver. This is the version of
    // the autobaud command that is not tied to a particular
    // Sabertooth object.
    Sabertooth::autobaud(UART_SABERTOOTH);

    UART_SABERTOOTH.setTimeout(C110P_MOTOR_SERIAL_TIMEOUT_MS);

    // This setting does not persist between power cycles.
    // See the Packet Serial section of the documentation for what values to use
    // for the minimum voltage command. It may vary between Sabertooth models
    // (2x25, 2x60, etc.).
    //
    // On a Sabertooth 2x25, the value is (Desired Volts - 6) X 5.
    // So, in this sample, we'll make the low battery cutoff 12V: (12 - 6) X 5 = 30.
    // sabertoothDome.setMinVoltage(30);

    // Setup the Drive motors
    sabertoothDiff.SetSpeedLimit(C110P_DRIVE_MAXIMUM_SPEED);
    sabertoothDiff.SetSafetyEnabled(C110P_MOTOR_SAFETY);
    sabertoothDiff.SetExpiration(C110P_MOTOR_SAFETY_TIMEOUT_MS);
    sabertoothDiff.SetRampingValue(C110P_DRIVE_RAMPING_PERIOD);
    sabertoothDiff.SetDeadband(C110P_DRIVE_DEADBAND);
    sabertoothDiffDrive.GetMotor(1).SetInverted(C110P_DRIVE_MOTOR_1_INVERTED);
    sabertoothDiffDrive.GetMotor(2).SetInverted(C110P_DRIVE_MOTOR_2_INVERTED);
    
    // Setup the Dome motor
    sabertoothSyRen.SetSpeedLimit(C110P_DOME_MAXIMUM_SPEED);
    sabertoothSyRen.SetSafetyEnabled(C110P_MOTOR_SAFETY);
    sabertoothSyRen.SetExpiration(C110P_MOTOR_SAFETY_TIMEOUT_MS);
    sabertoothSyRen.SetRampingValue(C110P_DOME_RAMPING_PERIOD);
    sabertoothSyRen.SetDeadband(C110P_DOME_DEADBAND);
    sabertoothSyRenDrive.GetMotor(1).SetInverted(C110P_DOME_MOTOR_1_INVERTED);

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
    // TODO: should all servers return to their home poistion on startup?

    // ref: https://github.com/plerup/espsoftwareserial/blob/main/README.md
    // set timeout for get commands which wait for 4 bytes of data 
    // maestro-arduio library only blocks for 2 bytes, but we double to 4 it to be safe
    // assume 8 bit, even parity, 2 stop bits = 11 bits per byte (worst case)
    uint16_t timeout = ceil(4.0 / ceil(MAESTRO_SERIAL_BAUD_RATE / 11.0 / 1000.0));
    Console.printf("Maestro timeout: %u %u %u\n", timeout, MAESTRO_SERIAL_BAUD_RATE, ceil(MAESTRO_SERIAL_BAUD_RATE / 11 / 1000));

    // Ensure timeout is less than CONFIG_ESP_TASK_WDT_TIMEOUT_S by at least 100
    if (timeout >= CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000 - 100) {
        Console.printf("Maestro timeout exceeds TASK WATCHDOG changing: %u -> %u\n", timeout, CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000 - 100);
        timeout = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000 - 100;
    }

    maestroBody.setTimeout(timeout);
    maestroDome.setTimeout(timeout);

    // Disable PWM signals to servos
    maestroBody.disableAll();
    maestroDome.disableAll();
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

int brightness = 0;  // how bright the LED is
int fadeAmount = 5;  // how many points to fade the LED by
// Arduino loop function. Runs in CPU 1.
void loop() {
    // This call fetches all the controllers' data.
    // Call this function in your main loop.
    bool dataUpdated = BP32.update();
    if (dataUpdated)
    {
        myControllers.processInputs();
    }

    brightness = brightness + fadeAmount;
    if (brightness <= 0 || brightness >= 255) {
      fadeAmount = -fadeAmount;
    }
    // The main loop must have some kind of "yield to lower priority task" event.
    // Otherwise, the watchdog will get triggered.
    // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
    // Detailed info here:
    // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time

    //     vTaskDelay(1);
    analogWrite(PIN_LED_FRONT, brightness);
    delay(150);
    // vTaskDelay(pdMS_TO_TICKS(10));
}