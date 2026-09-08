cmake_minimum_required(VERSION 3.12)

set(CMAKE_VERBOSE_MAKEFILE ON)

file(GLOB HOST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/vmu*.c*")
add_executable(vmu ${HOST_SRC})
pico_add_extra_outputs(vmu)
target_link_libraries(vmu
  PRIVATE
    pico_multicore
    hal-MapleBus
    hal-System
    hal-Usb-Client-Hid
    pico_stdio_usb
    hostLib
)
target_compile_options(vmu PRIVATE
  -Wall
  -Werror
  -O0
  -g
)

target_include_directories(vmu
  PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vmu>"
    "${PROJECT_SOURCE_DIR}/inc")
