# C1-10P for ESP32 microcontroller

This repo contains a template for controlling an Astromech containing:
- Penumbra
- [Dimension Engineering Sabertooth 2x32](https://www.dimensionengineering.com/products/sabertooth2x32)
- [Dimension Engineering SyRen 10](https://www.dimensionengineering.com/products/syren10)
- [SparkFun MP3 Trigger](https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24)
- [Polou Maestro USB Servo Controllers](https://www.pololu.com/category/102/maestro-usb-servo-controllers)
- Bluetooth Controller

## Developer Environment Setup
This follows the Bluepad32 setup for an environment:
https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template

This typically includes:
- [Visual Studio Code](https://code.visualstudio.com/download)
- [Platform.IO Extension](https://platformio.org/install/ide?install=vscode)

As a backup, you can download the offline install for ESP-IDF 5.3.2 (as of January 2025):
- [ESP-IDF 5.3](https://dl.espressif.com/dl/esp-idf/?idf=5.3)

### Microsoft Windows (extra steps)
- [GitBash](https://git-scm.com/downloads)
- [GNUWIN32](http://gnuwin32.sourceforge.net/install.html)
    - Add location of `make.exe` to system PATH

## How to alter .patch files
In order to convert native Arduino libraries to compile with the ESP-IDF, some of them need to adapt to
use CMake. There are also some libraries which require altering additional code within to function on
ESP hardware.

This is accomplished with diff/patch. The patches are created by the following process:
1. `make {x}-download` will download the specified branch/version for Arduino
2. Make changes to the `build/{x}/_new` folder for the changes required
3. `make {x}-patch` to generate the content of the `patches/components/{X}.patch` (verify this is what you expect)
4. `make {x}` to re-download the specified branch/version for Arduino, apply the patch, and move the contents to the `components/{x}` directory

## Libraries
Refer to [components/README.md](components/README.md)

## Bluetooth Mac Addresses
Create a file in `main/include/SettingsBluetooth.h` with settings containing the MAC address of controllers you wish to restrict
connecting to the ESP32.

You can rename [main/include/SettingsBluetooth.h.example]() to `main/include/SettingsBluetooth.h` with your addresses.