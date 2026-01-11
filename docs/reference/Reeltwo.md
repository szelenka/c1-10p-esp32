# Reeltwo Reference

[Reeltwo](https://github.com/reeltwo/Reeltwo) code is located in build/Reeltwo folder, if not present clone the repository to that location.

## Summary

### Pros

- Mature project widely used for puppeting R-Series astromech robots.
- Supports variety of common Servo Controllers (Maestro, PWM, etc.), DC Motor Controllers (Cytron, Sabertooth, etc.), and LED Controllers.
- AnimatedEvent and AnimationPlayer classes provide a consistent method to puppet actuators and sensors throughout the program and robot; almost similar to Keyframes.
- Web interface for modifying parameters in real-time.

### Cons

- Tightly coupled with R-Series robot peripherals, not transferrable to other actuators and sensors which make up other astromech robots.
- Statically links actuators and sensors into robot body parts of R-Series robots (i.e. Dome/Body/Legs) rather than reusable components like Nodes from ROS2.
- JoystickController class is tightly coupled with program execution, making it difficult to swap the Bluetooth Controller for a different type; or change the input mapping to suit a different control mechanism.
- Organization of source code dumps everything into early Arduino style, making it difficult to navigate and debug.
- Basing every component off AnimatedEvent limits portability; sequencing AnimatedEvents is not intuitive.

## Notable Files

### Core Setup for multiple microcontrollers

- src/ReelTwo.h
- src/core/BatteryMonitor.h

### Core Animation Sequencer

- src/core/AnalogMonitor.h
- src/core/AnimatedEvent.h
- src/core/Animation.h

### Dome Spinner

- src/drive/DomeDrive.h
- src/drive/DomeDrivePWM.h
- src/drive/DomeDriveSabertooth.h
- src/drive/DomeDriveSerial.h
- src/drive/DomePosition.h
- src/drive/DomePositionProvider.h
- src/drive/DomeSensorAnalogPosition.h

### Differential Drive

- src/drive/TankDrive.h
- src/drive/TankDrivePWM.h
- src/drive/TankDriveSabertooth.h
- src/drive/TankDriveSerial.h
- src/drive/TargetSteering.h

### Joystick Controller

- src/bt/PSController/PSController.h
- src/JoystickController.h

### Servo Controls

- src/ServoDispatch.h
- src/ServoDispatchDirect.h
- src/ServoDispatchPCA9685.h
- src/ServoEasing.h
- src/ServoSequencer.h
- src/ServoSettings.h
