cmake_minimum_required(VERSION 3.22.1)

project("hamclock")

set(HC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../ESPHamClock")

add_compile_definitions(
    _WEB_ONLY
    _CLOCK_1600x960
    NO_UPGRADE
    NO_SYSTEM_CONTROLS
)

include_directories(
    ${HC_DIR}
    ${HC_DIR}/ArduinoLib
    ${HC_DIR}/wsServer/include
    ${HC_DIR}/zlib-hc
)

file(GLOB HC_SRCS
    "${HC_DIR}/*.cpp"
)

file(GLOB ARDUINO_SRCS
    "${HC_DIR}/ArduinoLib/*.cpp"
)

file(GLOB WS_SRCS
    "${HC_DIR}/wsServer/src/*.cpp"
)

file(GLOB ZLIB_SRCS
    "${HC_DIR}/zlib-hc/*.cpp"
)

add_compile_options(
    "-include" "${CMAKE_CURRENT_SOURCE_DIR}/ifaddrs_compat.h"
)

add_library(
    hamclock
    SHARED
    hamclock_jni.cpp
    ifaddrs_compat.c
    ${HC_SRCS}
    ${ARDUINO_SRCS}
    ${WS_SRCS}
    ${ZLIB_SRCS}
)

find_library(
    log-lib
    log
)

target_link_libraries(
    hamclock
    ${log-lib}
)

target_link_options(
    hamclock
    PRIVATE
    "-Wl,-z,max-page-size=16384"
    "-Wl,--wrap=_ZN4WiFi10macAddressEv"
    "-Wl,--wrap=accept"
    "-Wl,--wrap=accept4"
)


