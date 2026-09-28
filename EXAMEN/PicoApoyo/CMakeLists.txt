# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Board configurada específicamente para Pico 2 W (RP2350 con Wi-Fi)
set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(PicoApoyo C CXX ASM)

pico_sdk_init()

add_executable(PicoApoyo
    PicoApoyo.c
)

pico_set_program_name(PicoApoyo "PicoApoyo")
pico_set_program_version(PicoApoyo "1.0")

# Habilitar stdio por USB y deshabilitar por UART
pico_enable_stdio_usb(PicoApoyo 1)
pico_enable_stdio_uart(PicoApoyo 0)

target_link_libraries(PicoApoyo
    pico_stdlib
    hardware_uart
)

target_include_directories(PicoApoyo PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(PicoApoyo)