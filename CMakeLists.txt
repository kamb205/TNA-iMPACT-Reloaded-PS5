# tnaimpact - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(tnaimpact LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(TNAIMPACT_SOURCES
    src/main.cpp
    src/input_script.cpp
)

if(WIN32)
    add_executable(tnaimpact WIN32 ${TNAIMPACT_SOURCES})
else()
    add_executable(tnaimpact ${TNAIMPACT_SOURCES})
endif()

rexglue_setup_target(tnaimpact)
