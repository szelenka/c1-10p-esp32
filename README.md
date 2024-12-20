# C1-10P for ESP32 microcontroller

This repo contains a template for controlling an Astromech containing:
- Penumbra
- [Dimension Engineering Sabertooth 2x32](https://www.dimensionengineering.com/products/sabertooth2x32)
- [Dimension Engineering SyRen 10](https://www.dimensionengineering.com/products/syren10)
- [SparkFun MP3 Trigger](https://learn.sparkfun.com/tutorials/mp3-trigger-hookup-guide-v24)
- [Polou Maestro USB Servo Controllers](https://www.pololu.com/category/102/maestro-usb-servo-controllers)
- Bluetooth Controller

## Safety
Ensure all motors stop when bluetooth controller has not responded in a specified timeframe:
- https://bluepad32.readthedocs.io/en/latest/FAQ/#how-to-detect-when-a-gamepad-is-out-of-range

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
