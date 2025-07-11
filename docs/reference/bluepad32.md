# bluepad32 Reference

[bluepad32_arduino](https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template/) code is located in components/bluepad32_arduino


#### Pros

- Supports most, if not all, modern Bluetooth gamepads, mice and keyboards.
- Built-in PCB Antenna support from ESP32 
- Easy extensions to create your own platform to interact with Bluetooth controllers.

#### Cons

- Not explicitly designed for astromech robots, would require custom drive system and motor safety implementation.
- Consumes a full core of ESP32, to manage the Bluetooth connection, and consumes a Hardware Serial port to communicate between cores.

## Notable Files

### Link between Arduino and Bluepad32

- components/bluepad32_arduino/include/arduino_bootstrap.h
- components/bluepad32_arduino/include/arduino_platform.h
- components/bluepad32_arduino/include/ArduinoBluepad32.h
- components/bluepad32_arduino/include/ArduinoConsole.h
- components/bluepad32_arduino/include/ArduinoController.h
- components/bluepad32_arduino/include/ArduinoControllerData.h
- components/bluepad32_arduino/include/ArduinoControllerProperties.h
- components/bluepad32_arduino/include/ArduinoGamepad.h
- components/bluepad32_arduino/include/ArduinoGamepadProperties.h
- components/bluepad32_arduino/include/Bluepad32.h
- components/bluepad32_arduino/arduino_platform.c
- components/bluepad32_arduino/ArduinoBluepad32.cpp
- components/bluepad32_arduino/ArduinoConsole.cpp
- components/bluepad32_arduino/ArduinoController.cpp
- components/bluepad32_arduino/ArduinoGamepad.cpp

### Entrypoint

- main/main.c
- main/sketch.cpp
