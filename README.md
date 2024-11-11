# C1-10P for ESP32 microcontroller

## Safety
Ensure all motors stop when bluetooth controller has not responded in a specified timeframe:
- https://bluepad32.readthedocs.io/en/latest/FAQ/#how-to-detect-when-a-gamepad-is-out-of-range

## Libraries
### Bluepad32
To connect any supported Bluetooth controller to the ESP32, we build upon the Bluepad32 C library:
- https://github.com/ricardoquesada/bluepad32

This uses the Expressif's libraries and other BTStack components, which make it tricky to compile with the default Arduino IDE. As such, it's recommended
to build upon the Bluepad32 Arduino Template outlined here:
- https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template

### SoftwareSerial
The default compiler for ESP32 does not include an implementation of SoftwareSerial. Where we can, the ESP32 board should use the hardware UARTs, but 
their may be more Serial connections than UARTs available. For this we use SoftwareSerial library:
- SoftwareSerial : https://github.com/plerup/espsoftwareserial
- HardwareSerial (Pin remaping)
    - https://docs.espressif.com/projects/esp-idf/en/v4.4.8/esp32/api-reference/peripherals/uart.html
    - https://stackoverflow.com/questions/60094545/esp32-softwareserial-library

### Arduino Libraries for SyRen/Sabertooth Serial
The original [Sabertooth Arduino Library](https://www.dimensionengineering.com/info/arduino) leverages implicit casting of byte to int, which the Arduino ESP32 compiler doesn't like. There are two approaches to mitigate this:
1. Consume a modified version of the official library, which uses `static_cast<int>` https://github.com/dominicklee/Sabertooth-for-ESP32
2. Create a modified SabertoothDriver version, which uses `(int)` https://github.com/reeltwo/Reeltwo (src/motor/SabertoothDriver.h)

### Pololu Maestro Servo Controller library for Arduino
To control animations from connected servos, the [Polou Maestro USB Servo Controllers](https://www.pololu.com/category/102/maestro-usb-servo-controllers) are used and use standard Serial to communicate with the ESP32.
- https://github.com/pololu/maestro-arduino

### SparkFun MP3 Trigger
For triggering sound effects, the [SparkFun MP3 Trigger](https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24) is leveraged, which uses standard Serial to communicate with the ESP32.
- https://github.com/sansumbrella/MP3Trigger-for-Arduino

### Units
https://docs.wpilib.org/en/stable/docs/software/basic-programming/cpp-units.html
https://github.com/nholthaus/units

### WPILib
https://github.com/wpilibsuite/allwpilib

### fmtlib/fmt
https://github.com/espressif/idf-extra-components/tree/master/fmt