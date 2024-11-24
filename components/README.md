# List of Third-Party Components

This folder contains all third-party libraries used. The following table lists their source, license type, and what it is used for.

| Library                                                                                                         | Version  | License       | Used             | Notes |
|-----------------------------------------------------------------------------------------------------------------|----------|---------------|------------------|-------|
| [arduino](https://github.com/espressif/arduino-esp32/)                                                          | 2.0.17   | LGPL-2.1      | Interface to microcontrollers              | N/A   |
| [bluepad32](https://github.com/ricardoquesada/bluepad32/)                                                       | 4.1.0    | Apache-2.0    | Interface to Bluetooth Controllers              | N/A   |
| [bluepad32_arduino](https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template/)                      | 4.1.0    | Apache-2.0    | Middleware between arduino and bluepad32              | N/A   |
| [btstack](https://github.com/bluekitchen/btstack/)                                                              | 1.6.1    | Common Clause  | Dependency for bluepad32              | Similar to BSD-3-Clause, with clause prohibiting Commercial use   |
| [esp-idf](https://github.com/espressif/esp-idf)                                                                 | 4.4.8    | Apache-2.0    | cmd_nvs, cmd_system, pthread |  Espressif Build system |
| [espsoftwareserial](https://github.com/plerup/espsoftwareserial/)                                               | 8.2.0    | LGPL-2.1      | Provide SoftwareSerial for ESP microcontrollers              | N/A   |
| [ghostl](https://github.com/dok-net/ghostl/)                                                                    | 1.0.1    | LGPL-2.1      | Dependency for espsoftwareserial              | N/A   |
| [maestro-arduino](https://github.com/pololu/maestro-arduino/)                                                   | 1.0.0    | MIT           | Interface to Maestro board(s) over UART              | N/A   |
| [MP3Trigger](https://github.com/sansumbrella/MP3Trigger-for-Arduino)                                            | 5b70369      | Public Domain | Interface to MP3 Trigger board over UART             | N/A   |
| [Reeltwo](https://github.com/reeltwo/Reeltwo)                     | 23.5.3  | LGPL-2.1    | Style/Reference inspiration  | Cherrypick to work with bluepad32 |
| [Sabertooth](https://www.dimensionengineering.com/)                                                             | N/A      | MIT-0         | Interface to SyRen/Sabertooth board(s) over UART              | Patch to compile with ESP-IDF ecosystem   |
| [WPIlibc](https://github.com/wpilibsuite/allwpilib/)                                                            | v2024.3.2| BSD-3-Clause  | Style/Reference inspiration              | Differential Drive and Motor Safety   |


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
3. Apply patch on original library

### Pololu Maestro Servo Controller library for Arduino
To control animations from connected servos, the [Polou Maestro USB Servo Controllers](https://www.pololu.com/category/102/maestro-usb-servo-controllers) are used and use standard Serial to communicate with the ESP32.
- https://github.com/pololu/maestro-arduino

### SparkFun MP3 Trigger
For triggering sound effects, the [SparkFun MP3 Trigger](https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24) is leveraged, which uses standard Serial to communicate with the ESP32.
- https://github.com/sansumbrella/MP3Trigger-for-Arduino

### Units
-- TODO: do we want to purge this?
https://docs.wpilib.org/en/stable/docs/software/basic-programming/cpp-units.html
https://github.com/nholthaus/units

### WPILib
-- we only use the WPI library, how to automate pull?
https://github.com/wpilibsuite/allwpilib

### fmtlib/fmt
-- not currently using
https://components.espressif.com/components/espressif/fmt
https://github.com/espressif/idf-extra-components/tree/master/fmt