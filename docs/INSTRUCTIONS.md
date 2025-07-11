# Thought Process

The main purpose of this repository is to provide a framework for puppeting astromech robots with a Bluetooth Controller. 

The framework must be easy to learn and adapt for aspiring programmers to control a wide variety of actuators and sensors which make up any astromech robot.

This repository must follow similar concepts found in ROS2, but be more focused for executing on a single ESP32. At the same time, it must be extensible to join a ROS2 network (if needed). 

Real-time computing is a key feature of many robotics systems, particularly safety and mission-critical applications. 

To make a real-time computer system, our real-time loop must update periodically to meet deadlines. We can only tolerate a small margin of error on these deadlines (our maximum allowable jitter). To do this, we must avoid nondeterministic operations in the execution path, things like: pagefault events, dynamic memory allocation/deallocation, and synchronization primitives that block indefinitely.

## Inspiration

The inspiration for this repository is spread across many existing approaches to solve similar problems, but each have their own pros/cons which we need to address in this repository. For a detailed analysis of each inspiration target, follow the links below:

- Reeltwo: [Reeltwo Reference](./reference/Reeltwo.md)
- Padawan360: [Padawan360 Reference](./reference/Padawan360.md)
- SHADOW: [SHADOW Reference](./reference/SHADOW.md)
- Penumbra: [Penumbra Reference](./reference/Penumbra.md)
- PenumbraShadowMD: [PenumbraShadowMD Reference](./reference/PenumbraShadowMD.md)
- bluepad32: [bluepad32 Reference](./reference/bluepad32.md)
- ROS2: [ROS2 Reference](./reference/ROS2.md)
- micro-ROS: [micro-ROS Reference](./reference/micro-ROS.md)
- WPILib: [WPILib Reference](./reference/WPILib.md)

## Structure

- Orchestration of setup, build, test, and flash must be performed through an easy to understand Makefile and PlatformIO CLI tools.
- The code must be easy for aspiring Computer Programmers to understand and adapt, following generally agreed upon industry best practices for C/C++ development.
- It must be self-contained, with all dependencies either linked and pinned to a specific (recent) version in the platformio.ini file or directly included in the components folder.
    - In the event there's a Library we use which is not compatible with the ESP-IDF or PlatformIO, we need to produce a .patch file in the patches folder, and update the Makefile to download and apply the patch while locating the patched file in the components folder.
- It must be extensible, allowing for easy addition of new components and features without deviating from the core structure or style.
- It must be compatible for Windows, Linux, and macOS build environments.

## Style

- Use C++17 or later
- Use ESP-IDF, CMake, and PlatformIO for build system
- Use Doxygen for documentation
- Use Git for version control
- Use Markdown for documentation
- Use Clang for code analysis

## Functionality

### Actuators

#### DC Motors

- Sabertooth Controller (but easy to extend to other controllers)
- Single DC Motor
- Dual DC Motor (i.e. Differential Drive)
- Quad DC Motor (i.e. Mecanum Drive)
- Smoothing via SlewRateLimiter to protect motors from damage

#### Servos

- Maestro Controller (but easy to extend to other controllers)
- Control Servo position manually
- Toggle Servo position to preset positions
- Control multiple Servo positions in parallel
- Servo Easing/Smoothing algorithms for smooth easing into start/stop position
- Track Servo Position
- RSS Mechanism control for Dome

#### Other

- Blink LED to a preset patterns
- Toggle audio from playing a sound file
- Syncronize LED patterns with audio from playing a Sound file
- Control volume of audio from playing a Sound file

### Sensors

- Battery Monitor
- Camera boundary box (i.e. face detection, brightness, etc.)
- Analog Potentiometer position (Dome position sensor)

### Controller

- Restrict which Bluetooth Controller MAC Addresses are allowed to connect.
- Configurable assignment of MAC Address of Bluetooth Controllers to perform specific functions (i.e. Drive, Dome, Camera, Sound, etc.)
- Initial target Bluetooth Controller are Nintendo Switch JoyCon L/R (but easy to extend to other controllers)
- Future expansion of a Microcontroller connected over WiFi or Bluetooth (custom Vambrace) 
- Track Button State for individual button press, release, hold duration, double-press, sequencing, etc.

### Communication

- Bluetooth (for Controller input)
- WiFi (for parameter configuration, and telemetry)
- Serial (for communicating with other microcontrollers inside the robot, and telemetry over USB)

#### Serial Protocol

- lightweight
- error checking
- extendable to add new commands
- concept of source and destination in payload
- easy to implement in C/C++ and MicroPython/CircuitPython in non-blocking manner

### Safety

- Configurable timeout for when no controller input is received, stop all actuators
- Configurable timeout for when loop takes too long to iterate, stop all actuators
- Blink LED sequence and/or play sound to indicate faults (i.e. controller timeout, actuator timeout, etc.)

### User Interface

- Web Interface for modifying parameters in real-time.
- Bluetooth Controller input mapping to function execution.

### Logging

- Configurable logging level
- Configurable logging output (i.e. Serial, WiFi, USB thumb drive, etc.)
- Configurable telemetry output of Actuator and Sensor activity
