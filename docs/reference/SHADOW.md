# SHADOW Reference

[SHADOW](https://gitlab.com/darren-blum/SHADOW) code is located in build/SHADOW folder, if not present clone the repository to that location.

## Summary

### Pros

- SoftwareSerial overflow buffer checking; prioritize input from controller for MotorSafety
- Some components are separated into reusable Arduino Libraries.
- Analog input from PS3Nav Bluetooth Controller allows for variable speed control of DC Motors.

### Cons

- Explicitly for only PS3Nav Bluetooth Controllers.
- Bluetooth Controller input is tightly coupled with program execution, making it difficult to swap the Bluetooth Controller for a different type; or change the input mapping to suit a different controller.
- Organization of source code dumps everything into early Arduino style (i.e. monolithic .ino file), making it difficult to navigate and debug.

## Notable Functions

Located in Shadow.ino

- automateDome
- ps3DomeDrive
- ps3utilityArms
- processSoundCommand
- ps3soundControl
- domeDrive
- soundControl
