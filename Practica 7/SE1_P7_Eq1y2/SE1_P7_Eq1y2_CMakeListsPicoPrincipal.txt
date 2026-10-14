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

# Pico principal: Raspberry Pi Pico normal (RP2040).
# Si fuera una Pico 2 (RP2350), cambia a: pico2
set(PICO_BOARD pico CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(pwm_console C CXX ASM)
pico_sdk_init()
add_executable(pwm_console PicoPrincipal.c)
pico_set_program_name(pwm_console "pwm_console")
pico_set_program_version(pwm_console "1.0")

# La consola va por UART0 (GP0/GP1) hacia la Pico de apoyo: sin stdio
pico_enable_stdio_uart(pwm_console 0)
pico_enable_stdio_usb(pwm_console 0)
target_link_libraries(pwm_console
    pico_stdlib
    hardware_pwm
    hardware_clocks
    hardware_uart
    hardware_irq
)

pico_add_extra_outputs(pwm_console)

