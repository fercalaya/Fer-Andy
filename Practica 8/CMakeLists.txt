cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2 CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(practica_adc C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(lib/consola)

add_executable(practica_adc main.c adc_stream.c)
target_link_libraries(practica_adc consola pico_stdlib hardware_adc hardware_pwm hardware_clocks)
pico_add_extra_outputs(practica_adc)
