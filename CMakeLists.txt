cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
set(FREERTOS_KERNEL_PATH "D:/FreeRTOS-Kernel")
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

project(EstacaoDeMonitoramento C CXX ASM)
pico_sdk_init()

include_directories(${CMAKE_SOURCE_DIR}/lib)

add_executable(EstacaoDeMonitoramento EstacaoDeMonitoramento.c lib/ssd1306.c)

pico_generate_pio_header(EstacaoDeMonitoramento ${CMAKE_CURRENT_LIST_DIR}/lib/ws2812.pio)

pico_set_program_name(EstacaoDeMonitoramento "EstacaoDeMonitoramento")
pico_set_program_version(EstacaoDeMonitoramento "0.1")

pico_enable_stdio_uart(EstacaoDeMonitoramento 1)
pico_enable_stdio_usb(EstacaoDeMonitoramento 1)

target_link_libraries(EstacaoDeMonitoramento
        pico_stdlib
        hardware_gpio
        hardware_i2c
        hardware_adc
        hardware_pwm
        hardware_pio
        FreeRTOS-Kernel
        FreeRTOS-Kernel-Heap4
        )
        
target_include_directories(EstacaoDeMonitoramento PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/lib
)

pico_add_extra_outputs(EstacaoDeMonitoramento)

