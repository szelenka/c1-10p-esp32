# WPIlib Reference

The WPI Robotics Library (WPILib) is the standard software library provided for teams to write code for their FRC® robots. WPILib contains a set of useful classes and subroutines for interfacing with various parts of the FRC control system (such as sensors, motor controllers, and the driver station), as well as an assortment of other utility functions.

[wpilibc](https://github.com/wpilibsuite/allwpilib/) code is located in build/allwpilib folder, if not present clone the repository to that location.

The C++ code typically resides under the src/main/native/cpp or components folders. The C++ code may not be directly compatible with an ESP32 microcontroller.

### Pros

- Main purpose is to help accelerate development of robot software to compete in First Robotics Competition (FRC), for High School and Undergrad students. (Should be easy to learn and adapt)
- 

### Cons

- Originally targetted for native C++ applications, not natively compatible with ESP32.
- Organization is complex, mixing Java and C++ code into a single repository.
- Build system is not compatible with ESP32.
- 

## RobotBuilder

### Subsystems

Subsystems are classes that encapsulate (or contain) all the data and code that make a subsystem on your robot operate. The first step in creating a robot program with the RobotBuilder is to identify and create all the subsystems on the robot. Examples of subsystems are grippers, ball collectors, the drive base, elevators, arms, etc. Each subsystem contains all the sensors and actuators that are used to make it work. For example, an elevator might have a Victor SPX motor controller and a potentiometer to provide feedback of the robot position.

#### Components 

Each subsystem consists of a number of actuators, sensors, and controllers that it uses to perform its operations. These sensors and actuators are added to the subsystem with which they are associated. Each of the sensors and actuators comes from the RobotBuilder palette and is dragged to the appropriate subsystem. For each, there are usually other properties that must be set such as port numbers and other parameters specific to the component.

#### Commands

Commands are distinct goals that the robot will perform. These commands are added by dragging the command under the “Commands” folder. When creating a command, there are 7 choices:

Normal commands - these are the most flexible command, you have to write all of the code to perform the desired actions necessary to accomplish the goal.

Timed commands - these commands are a simplified version of a command that ends after a timeout

Instant commands - these commands are a simplified version of a command that runs for one iteration and then ends

Command groups - these commands are a combination of other commands running both in a sequential order and in parallel. Use these to build up more complicated actions after you have a number of basic commands implemented.

Setpoint commands - setpoint commands move a PID Subsystem to a fixed setpoint, or the desired location.

PID commands - these commands have a built-in PID controller to be used with a regular subsystem.

Conditional commands - these commands select one of two commands to run at the time of initialization.

### Operator Interface

The operator interface consists of joysticks, gamepads, and other HID input devices. You can add operator interface components (joysticks, joystick buttons) to your program in RobotBuilder. It will automatically generate code that will initialize all of the components and allow them to be connected to commands.

The operator interface components are dragged from the palette to the “Operator Interface” folder in the RobotBuilder program. First (1) add Joysticks to the program then put buttons under the associated joysticks (2) and give them meaningful names, like ShootButton.

## Notable Components

### MotorSafety

Found in wpilibc/src/main/native/include/frc/MotorSafety.h and wpilibc/src/main/native/cpp/MotorSafety.cpp

Follows a Watchdog pattern, to execute StopMotor after a timeout has expired with no Feed

### Timer

Found in wpillibc/src/main/native/include/frc/Timer.h and wpilibc/src/main/native/cpp/Timer.cpp

This calls to a FPGA clock to get a consistent time. It uses native C++ calls to manipulate the timer, which is not optimized for different compute platforms.

### SlewRateLimter

Found in wpimath/src/main/native/include/frc/filter/SlewRateLimiter.h and wpimath/src/main/native/cpp/SlewRateLimiter.cpp

This is a filter that limits the rate of change of an input value. It is useful for limiting the rate of change of a motor or other actuator.

