set(include_dirs
        "include")

set(src_dirs
        "."
        "chopper"
        "chopper/drive"
        "chopper/motorController")

# set(srcs 
#         "main.c"
#         "sketch.cpp")

set(requires 
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
idf_component_register(SRC_DIRS "${src_dirs}"
                INCLUDE_DIRS "${include_dirs}"
                REQUIRES "${requires}")

# some components inspired by wpilibc make heavy use of C++/20 standard
# ref: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html#c-language-standard
target_compile_options(${COMPONENT_LIB} PUBLIC -std=gnu++2a)
