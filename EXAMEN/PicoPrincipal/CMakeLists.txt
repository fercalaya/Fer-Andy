cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Board RP2040 estándar
set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(PicoPrincipal C CXX ASM)

pico_sdk_init()

add_executable(PicoPrincipal
    PicoPrincipal.c
)

pico_set_program_name(PicoPrincipal "PicoPrincipal")
pico_set_program_version(PicoPrincipal "1.0")

# Todo el tráfico se hace por hardware UART0, no por USB
pico_enable_stdio_uart(PicoPrincipal 0)
pico_enable_stdio_usb(PicoPrincipal 0)

target_link_libraries(PicoPrincipal
    pico_stdlib
    hardware_uart
    hardware_irq
    hardware_gpio
)

target_include_directories(PicoPrincipal PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(PicoPrincipal)