set(srcs
        "main.c"
        "sketch.cpp"
        "TankDrive.cpp"
        "TankDriveSabertooth.cpp")

set(requires 
        "bluepad32"
        "bluepad32_arduino"
        "arduino"
        "btstack"
        "espsoftwareserial"
        "maestro-arduino"
        "MP3Trigger"
        "Sabertooth")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    REQUIRES "${requires}")
