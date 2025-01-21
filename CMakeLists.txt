# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.2...3.13)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(COMPONENT_DIRS, $ENV{PROJECT_DIR}/components)

# set(EXTRA_COMPONENT_DIRS 
#         $ENV{PROJECT_DIR}/main/chopper
#         $ENV{PROJECT_DIR}/main/chopper/dome
#         $ENV{PROJECT_DIR}/main/chopper/drive
#         $ENV{PROJECT_DIR}/main/chopper/motorController
#         $ENV{PROJECT_DIR}/main/chopper/sensor)

project(c110p_controller_esp32)
