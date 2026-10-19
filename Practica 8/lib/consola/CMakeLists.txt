add_library(consola STATIC consola.c)
target_include_directories(consola PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(consola PUBLIC pico_stdlib hardware_uart hardware_irq)
