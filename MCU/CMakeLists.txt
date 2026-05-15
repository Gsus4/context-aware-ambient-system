# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(light_node_pico_c C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(light_node_pico_c 
    main.c

    drivers/ws2812.c
    drivers/bh1750.c

    services/led_service.c
    services/light_sensor_service.c
    services/auto_light_service.c
    services/mode_service.c
    services/command_parser.c
    services/flow_service.c

    utils/logger.c
    utils/status_report.c
    utils/error_code.c
    utils/color_utils.c

    comm/uart_comm.c
    )
pico_generate_pio_header(light_node_pico_c
    ${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812.pio
)

pico_set_program_name(light_node_pico_c "light_node_pico_c")
pico_set_program_version(light_node_pico_c "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(light_node_pico_c 1)
pico_enable_stdio_usb(light_node_pico_c 1)

# Add the standard library to the build
target_link_libraries(light_node_pico_c
        pico_stdlib
        hardware_pio
        hardware_watchdog
        )

# Add the standard include files to the build
target_include_directories(light_node_pico_c PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/drivers
        ${CMAKE_CURRENT_LIST_DIR}/services
        ${CMAKE_CURRENT_LIST_DIR}/utils
)

# Add any user requested libraries
target_link_libraries(light_node_pico_c 
        hardware_i2c
        pico_cyw43_arch_none
        hardware_pio
        pico_stdlib
        hardware_i2c
        hardware_pio
        hardware_clocks
        hardware_watchdog
        )

pico_add_extra_outputs(light_node_pico_c)

