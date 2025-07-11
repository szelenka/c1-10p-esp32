# Padawan 360 Reference

[Padawan 360](https://github.com/dankraus/padawan360) code is located in build/Padawan360 folder, if not present clone the repository to that location.

## Summary

### Pros

- Simple increase/decrease volume button sequence.
- Ability to chain button presses to perform different actions.
- Checking button press and release events is easy to read in the source code.

### Cons

- Different sections of the robot are separated into separate monolithic Arduino sketches, implying you need multiple microcontrollers with their own isolated code to function the full robot.
- XBOXRECV class is tightly coupled with program execution, making it difficult to swap the Bluetooth Controller for a different type; or change the input mapping to suit a different control mechanism.
- Basic implementation of a Differential Drive system, tightly coupled to XBOX 360 Bluetooth Controller.
- Different microcontroller code separated into different monolithic Arduino sketches, rather than a single sketch with define to split the logic at compile time.
- Limited Servo, DC Motor, etc. module support when compared to Reeltwo.
