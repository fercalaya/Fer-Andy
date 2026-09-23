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
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location

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


set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK

include(pico_sdk_import.cmake)

project(Picodeapoyo C CXX ASM)

# Initialise the Raspberry Pi Pico SDK

pico_sdk_init()

# Add executable

add_executable(Picodeapoyo Picodeapoyo.c)

pico_set_program_name(Picodeapoyo "Picodeapoyo")
pico_set_program_version(Picodeapoyo "0.1")

#usb para el serial monitor

pico_enable_stdio_usb(Picodeapoyo 1)

pico_enable_stdio_uart(Picodeapoyo 0)

# Add the standard library

target_link_libraries(Picodeapoyo
    pico_stdlib
)

target_include_directories(Picodeapoyo PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(Picodeapoyo)