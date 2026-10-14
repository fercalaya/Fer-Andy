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

# Pico de apoyo: Raspberry Pi Pico 2 W (RP2350 con WiFi)
set(PICO_BOARD pico2_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(uart_bridge C CXX ASM)
pico_sdk_init()
add_executable(uart_bridge PicoApoyo.c)
pico_set_program_name(uart_bridge "uart_bridge")
pico_set_program_version(uart_bridge "1.0")

# USB para el PC; el stdio por UART se apaga porque UART0 lo usa el puente
pico_enable_stdio_usb(uart_bridge 1)
pico_enable_stdio_uart(uart_bridge 0)

target_link_libraries(uart_bridge
    pico_stdlib
    hardware_uart
)
pico_add_extra_outputs(uart_bridge)