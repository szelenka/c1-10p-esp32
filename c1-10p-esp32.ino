#include <Arduino.h>
#include "src/settings/pin-map.h"
#include "src/settings/system.h"
#include "src/settings/user.h"

/*
    Bluepad32 Configuration
*/
#include <Bluepad32.h>

ControllerPtr myControllers[BP32_MAX_GAMEPADS];

/*
    DimensionEngineering Configuration
*/
#include <SoftwareSerial.h>
#include <Sabertooth.h>

// RX on no pin (unused), TX on pin from PINOUT.h connected to S1 bus
SoftwareSerial sabertoothSerial(NOT_A_PIN, PIN_SABERTOOTH_TX); 

Sabertooth sabertoothDome(128, sabertoothSerial); 
Sabertooth sabertoothFeet(129, sabertoothSerial);

/*
    Maestro Configuration
*/
#include <PololuMaestro.h>

// RX and TX on pin from PINOUT.h connected to opposite TX/RX on Maestro board
SoftwareSerial maestroBodySerial(PIN_MAESTRO_BODY_RX, PIN_MAESTRO_BODY_TX);
SoftwareSerial maestroDomeSerial(PIN_MAESTRO_DOME_RX, PIN_MAESTRO_DOME_TX);

MiniMaestro maestroBody(maestroBodySerial);
MiniMaestro maestroDome(maestroDomeSerial);

/*
    MP3 Configuration
*/
#include <MP3Trigger.h>

// RX and TX on pin from PINOUT.h connected to opposite TX/RX on MP3 Trigger board
SoftwareSerial mp3TriggerSerial(PIN_MP3TRIGGER_RX, PIN_MP3TRIGGER_TX);

MP3Trigger mp3Trigger;

/*
    OpenMV Configuration
*/

SoftwareSerial openMVSerial(PIN_OPENMV_RX, PIN_OPENMV_TX);

/* 
    Dome Position Configuration
*/
#include "drive/DomeSensorAnalogPosition.h"

DomeSensorAnalogPosition domePot(PIN_DOME_POTENTIOMETER);

/*
    Custom Configuration
*/
unsigned long lastUpdate = 0;


// This callback gets called any time a new gamepad is connected.
// Up to 4 gamepads can be connected at the same time.
void onConnectedController(ControllerPtr ctl) {
    bool foundEmptySlot = false;
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            Serial.printf("CALLBACK: Controller is connected, index=%d\n", i);
            // Additionally, you can get certain gamepad properties like:
            // Model, VID, PID, BTAddr, flags, etc.
            ControllerProperties properties = ctl->getProperties();
            Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", ctl->getModelName().c_str(), properties.vendor_id,
                           properties.product_id);
            myControllers[i] = ctl;
            foundEmptySlot = true;
            break;
        }
    }
    if (!foundEmptySlot) {
        Serial.println("CALLBACK: Controller connected, but could not found empty slot");
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    bool foundController = false;

    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            Serial.printf("CALLBACK: Controller disconnected from index=%d\n", i);
            myControllers[i] = nullptr;
            foundController = true;
            break;
        }
    }

    if (!foundController) {
        Serial.println("CALLBACK: Controller disconnected, but not found in myControllers");
    }
}

void dumpGamepad(ControllerPtr ctl) {
    Serial.printf(
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

void processGamepad(ControllerPtr ctl) {
    // There are different ways to query whether a button is pressed.
    // By query each button individually:
    //  a(), b(), x(), y(), l1(), etc...
    if (ctl->a()) {
        static int colorIdx = 0;
        // Some gamepads like DS4 and DualSense support changing the color LED.
        // It is possible to change it by calling:
        switch (colorIdx % 3) {
            case 0:
                // Red
                ctl->setColorLED(255, 0, 0);
                break;
            case 1:
                // Green
                ctl->setColorLED(0, 255, 0);
                break;
            case 2:
                // Blue
                ctl->setColorLED(0, 0, 255);
                break;
        }
        colorIdx++;
    }

    if (ctl->b()) {
        // Turn on the 4 LED. Each bit represents one LED.
        static int led = 0;
        led++;
        // Some gamepads like the DS3, DualSense, Nintendo Wii, Nintendo Switch
        // support changing the "Player LEDs": those 4 LEDs that usually indicate
        // the "gamepad seat".
        // It is possible to change them by calling:
        ctl->setPlayerLEDs(led & 0x0f);
    }

    if (ctl->x()) {
        // Some gamepads like DS3, DS4, DualSense, Switch, Xbox One S, Stadia support rumble.
        // It is possible to set it by calling:
        // Some controllers have two motors: "strong motor", "weak motor".
        // It is possible to control them independently.
        ctl->playDualRumble(0 /* delayedStartMs */, 250 /* durationMs */, 0x80 /* weakMagnitude */,
                            0x40 /* strongMagnitude */);
    }

    // Another way to query controller data is by getting the buttons() function.
    // See how the different "dump*" functions dump the Controller info.
    dumpGamepad(ctl);
}

void processControllers() {
    // TODO: 
    for (auto myController : myControllers) {
        if (myController && myController->isConnected()) {
            if (myController->hasData()) {
                processGamepad(myController);
                lastUpdate = millis();
            } else {
                // No data received from the controller.
                if ((millis() - lastUpdate) > C110P_CONTROLLER_TIMEOUT_MS) {
                    // If no data received in more than timeout, emergencyStop!
                    emergencyStop();
                    myController->disconnect();
                }
            }
        }
    }
}

void setupBluepad32() {
    Serial.printf("Firmware: %s\n", BP32.firmwareVersion());
    const uint8_t* addr = BP32.localBdAddress();
    Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Setup the Bluepad32 callbacks
    BP32.setup(&onConnectedController, &onDisconnectedController);

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
    sabertoothSerial.begin(SABERTOOTH_SERIAL_BAUD_RATE);

    // Autobaud is for the whole serial line -- you don't need to do
    // it for each individual motor driver. This is the version of
    // the autobaud command that is not tied to a particular
    // Sabertooth object.
    Sabertooth::autobaud(sabertoothSerial);

    // This setting does not persist between power cycles.
    // See the Packet Serial section of the documentation for what values to use
    // for the minimum voltage command. It may vary between Sabertooth models
    // (2x25, 2x60, etc.).
    //
    // On a Sabertooth 2x25, the value is (Desired Volts - 6) X 5.
    // So, in this sample, we'll make the low battery cutoff 12V: (12 - 6) X 5 = 30.
    sabertoothDome.setMinVoltage(30);
    sabertoothFeet.setMinVoltage(30);

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

    // setTimeout rounds up to the nearest 100 milliseconds
    // A value of 0 disables the serial timeout.
    sabertoothSerial.setTimeout(C110P_MOTOR_TIMEOUT_MS);
}

void debugMaestroPosition(Maestro &maestro) {
    for (uint8_t i = 0; i < 12; i++)
    {
        uint16_t position = maestro.getPosition(i);
        Serial.print("Channel: ");
        Serial.print(i);
        Serial.print(" Position: ");
        Serial.println(position);
    }
}

void setupMaestro() {
    // Set the serial baud rate.
    maestroBodySerial.begin(MAESTRO_SERIAL_BAUD_RATE);
    maestroDomeSerial.begin(MAESTRO_SERIAL_BAUD_RATE);
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

    mp3Trigger.setup(&mp3TriggerSerial);

    // Set the serial baud rate.
    mp3TriggerSerial.begin(MP3Trigger::serialRate());
}

void setupOpenMV() {
    // Set the serial baud rate.
    // TODO: is this fast enough?
    openMVSerial.begin(9600);
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
        processControllers();

    // blink LEDs pseudo-randomly?
    // digitalWrite(PIN_LED_FRONT, HIGH/LOW);
    // digitalWrite(PIN_LED_BACK, HIGH/LOW);

    // process messages off RX lines
    // TODO: maestroDome, maestroBody
    // TODO: openMV
    // TODO: mp3Trigger

    // The main loop must have some kind of "yield to lower priority task" event.
    // Otherwise, the watchdog will get triggered.
    // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
    // Detailed info here:
    // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time

    vTaskDelay(1);
}
