# Chopper Development Plan

Based on evaluation of docs/INSTRUCTIONS.md and all reference documents in docs/reference/, this plan outlines the development approach for the astromech robot framework.

## Original Framework Goals

The main purpose of this repository is to provide a framework for puppeting astromech robots with a Bluetooth Controller. The framework must be:

- Easy to learn and adapt for aspiring programmers
- Control a wide variety of actuators and sensors
- Follow ROS2-like concepts but focused for ESP32 execution
- Extensible to join ROS2 networks if needed
- Real-time computing with deterministic execution

## Updated Development Plan

### 1. **Architecture Redesign** (Major Change)
- **Adopt Node-Based Architecture**: Implement ROS2-like nodes but optimized for ESP32 single-core execution
- **Avoid Tight Coupling**: Unlike existing solutions (Reeltwo, SHADOW, Padawan360), create modular components that aren't hardcoded to specific robot parts
- **Component-Based Design**: Create reusable components instead of monolithic Arduino-style code

### 2. **Controller Framework** (Enhanced)
- **Use bluepad32 as Base**: Leverage its broad controller support but add custom abstraction layer
- **Decoupled Input Mapping**: Create configurable input-to-action mapping system (addressing major weakness in all existing solutions)
- **MAC Address Management**: Implement Penumbra-style controller restriction system
- **Button State Tracking**: Adopt Padawan360's button sequencing concepts but make them extensible

### 3. **Motion Control System** (Refined)
- **Keyframe Animation System**: Implement timeline-based control inspired by keyframes.md
- **WPILib Safety Patterns**: Adopt MotorSafety watchdog and SlewRateLimiter concepts
- **Interpolation Engine**: Build smooth motion transitions between keyframes
- **Multi-Controller Support**: Support Sabertooth, Maestro, and other controllers from Reeltwo but in modular fashion

### 4. **Real-Time Executive** (New Priority)
- **Deterministic Loop**: Implement strict timing requirements avoiding dynamic allocation
- **Priority-Based Scheduling**: Critical safety operations get highest priority
- **Watchdog Integration**: Multiple timeout systems (controller, loop execution, motor safety)

### 5. **Communication Architecture** (Enhanced)
- **Serial Protocol Design**: Lightweight, error-checked protocol for multi-microcontroller communication
- **Web Interface**: Adopt Penumbra/Reeltwo web parameter modification approach
- **Telemetry System**: Configurable logging and monitoring

### 6. **Safety-First Design** (Critical)
- **Multiple Watchdogs**: Controller timeout, execution timeout, motor safety
- **Graceful Degradation**: Safe shutdown procedures for all fault conditions
- **Status Indication**: LED patterns and audio feedback for system state

### 7. **Build System Integration** (Specific)
- **PlatformIO + ESP-IDF**: Hybrid approach supporting both ecosystems
- **Patch Management**: System for handling incompatible libraries
- **Cross-Platform**: Windows/Linux/macOS compatibility

## Key Insights from Reference Analysis

### Existing Solution Weaknesses to Avoid:
- **Monolithic Design**: All existing solutions suffer from tight coupling and Arduino-style organization
- **Controller Lock-in**: Every solution fails at controller flexibility - this is our major differentiator
- **Static Body Part Mapping**: Hardcoded to specific robot configurations instead of flexible components
- **Non-deterministic Execution**: Hobby-focused rather than real-time requirements

### Proven Concepts to Adopt:
- **Safety Patterns**: WPILib watchdog systems and motor protection
- **Animation Sequencing**: Reeltwo's AnimatedEvent concepts but more modular
- **Web Configuration**: Real-time parameter modification interfaces
- **Controller State Management**: Button press/release/hold/sequence tracking
- **Keyframe Interpolation**: Smooth motion planning between defined poses

## Implementation Phases

### Phase 1: Core Framework
1. **Environment Setup**: PlatformIO, ESP-IDF, build system
2. **Real-Time Executive**: Deterministic loop with watchdog systems
3. **Node Architecture**: Basic publish/subscribe system for ESP32
4. **Safety Framework**: Timeout management and fault handling

### Phase 2: Controller Integration
1. **bluepad32 Integration**: Controller discovery and connection
2. **Input Abstraction Layer**: Decoupled controller-to-action mapping
3. **Button State Engine**: Advanced input event processing
4. **Configuration System**: Runtime controller assignment and mapping

### Phase 3: Motion Control
1. **Actuator Abstraction**: Modular servo/motor controller interfaces
2. **Keyframe System**: Timeline-based motion planning
3. **Safety Integration**: MotorSafety watchdogs and SlewRateLimiters
4. **Interpolation Engine**: Smooth transitions between keyframes

### Phase 4: Communication & UI
1. **Serial Protocol**: Multi-microcontroller communication
2. **Web Interface**: Real-time parameter configuration
3. **Telemetry System**: Logging and monitoring
4. **Documentation**: Comprehensive guides and API docs

### Phase 5: Integration & Testing
1. **Hardware Integration**: Test with actual robot components
2. **Performance Optimization**: Real-time constraint validation
3. **Cross-Platform Testing**: Windows/Linux/macOS build verification
4. **Example Implementations**: Sample robot configurations

## Technical Standards

- **Language**: C++17 or later
- **Build System**: ESP-IDF, CMake, and PlatformIO
- **Documentation**: Doxygen
- **Version Control**: Git
- **Documentation Format**: Markdown
- **Code Analysis**: Clang
- **Real-Time Requirements**: Deterministic execution, no dynamic allocation in critical paths
- **Safety Standards**: Multiple watchdog systems, graceful degradation

## Success Criteria

1. **Modularity**: Components can be mixed/matched without code changes
2. **Controller Flexibility**: Any supported controller can control any robot function
3. **Real-Time Performance**: Meets strict timing deadlines with minimal jitter
4. **Safety Compliance**: Robust fault detection and safe shutdown procedures
5. **Ease of Use**: Aspiring programmers can understand and extend the framework
6. **Professional Quality**: Suitable for mission-critical robotic applications

This plan addresses the architectural limitations found in existing astromech robot frameworks while incorporating their proven concepts in a modern, modular, real-time system.