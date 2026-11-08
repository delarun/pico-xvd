cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{HOME}/pico-sdk)
endif()
set(PICO_BOARD pico CACHE STRING "Board")

include(pico_sdk_import.cmake)
project(pico_xvc C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(pico_xvc
    src/main.c
    src/usb_descriptors.c
    src/xvc_server.c
    src/jtag.c
    src/uart_bridge.c
    ${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dhserver.c
)

pico_generate_pio_header(pico_xvc ${CMAKE_CURRENT_LIST_DIR}/src/jtag.pio)

target_include_directories(pico_xvc PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${PICO_SDK_PATH}/lib/tinyusb/lib/networking
)

target_link_libraries(pico_xvc PRIVATE
    pico_stdlib
    pico_unique_id
    pico_bootrom
    hardware_pio
    hardware_clocks
    hardware_uart
    tinyusb_device
    pico_lwip_core
    pico_lwip_core4
    pico_lwip_netif
    pico_lwip_arch
    pico_usb_reset
)

# Интерфейс сброса для picotool (reboot -f / -u). Скорость 1200 бод обрабатывает uart_bridge.c,
# поэтому встроенную реакцию SDK на неё отключаем.
target_compile_definitions(pico_xvc PRIVATE
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
    PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=0
    PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0
    PICO_USB_RESET_BOOTSEL_ACTIVITY_LED=25
)

# Отладочный вывод — UART0 (GP0 TX / GP1 RX, 115200). USB занят сетевым интерфейсом.
pico_enable_stdio_uart(pico_xvc 1)
pico_enable_stdio_usb(pico_xvc 0)

pico_add_extra_outputs(pico_xvc)
