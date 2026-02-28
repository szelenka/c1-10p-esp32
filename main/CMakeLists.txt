set(include_dirs
        "include")

set(src_dirs
        "."
        "chopper"
        "chopper/core"
        "chopper/hal"
        "chopper/safety")

# NOTE: chopper/adapters and chopper/examples are excluded until
# Bluepad32ControllerSource is rewritten to not depend on the
# deleted ControllerDecorator. The old srcs list is removed since
# SRC_DIRS auto-discovers .cpp files.

set(requires
        "pthread"
        "bluepad32"
        "btstack")

# ref: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
idf_component_register(
        SRC_DIRS "${src_dirs}"
        INCLUDE_DIRS "${include_dirs}"
        REQUIRES "${requires}")

# some components inspired by wpilibc make heavy use of C++/20 standard
# ref: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html#c-language-standard
target_compile_options(${COMPONENT_LIB} PUBLIC -std=gnu++2a)
