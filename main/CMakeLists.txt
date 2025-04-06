set(include_dirs
        "include")

set(src_dirs
        "."
        "chopper"
        "chopper/drive"
        "chopper/motorController"
        "chopper/sensor")

set(srcs 
        "main.c"
        "sketch.cpp"
        "chopper/MotorSafety.cpp"
        "chopper/Timer.cpp"
        "chopper/drive/DifferentialDrive.cpp"
        "chopper/drive/DifferentialDriveSabertooth.cpp"
        "chopper/drive/RobotDriveBase.cpp"
        "chopper/drive/SingleDrive.cpp"
        "chopper/drive/SingleDriveSabertooth.cpp"
        "chopper/motorController/SabertoothController.cpp"
        "chopper/sensor/AnalogMonitor.cpp")

set(requires 
        "pthread"
        "bluepad32"
        "bluepad32_arduino"
        "arduino"
        "btstack"
        "ghostl"
        "espsoftwareserial"
        "maestro-arduino"
        "MP3Trigger"
        "Sabertooth")

# ref: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
idf_component_register(
        # SRCS "${srcs}"
        SRC_DIRS "${src_dirs}"
        INCLUDE_DIRS "${include_dirs}"
        REQUIRES "${requires}")

# some components inspired by wpilibc make heavy use of C++/20 standard
# ref: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html#c-language-standard
target_compile_options(${COMPONENT_LIB} PUBLIC -std=gnu++2a)

# COMPONENT_ADD_LDFLAGS
# COMPONENT_ADD_LINKER_DEPS