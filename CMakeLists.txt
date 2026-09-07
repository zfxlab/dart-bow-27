cmake_minimum_required(VERSION 3.22)

set(PNX_BOARD "h723_v1" CACHE STRING "Selected PNX board profile")
set(PNX_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/boards/${PNX_BOARD}")
if(NOT EXISTS "${PNX_BOARD_DIR}/board.cmake" OR NOT EXISTS "${PNX_BOARD_DIR}/board.json")
    message(FATAL_ERROR "Unknown or incomplete PNX_BOARD='${PNX_BOARD}'")
endif()
include("${PNX_BOARD_DIR}/board.cmake")

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(PNX_EMBEDDED_COMPILER_HINT "${CMAKE_C_COMPILER};${CMAKE_CXX_COMPILER}")
    if(PNX_EMBEDDED_COMPILER_HINT MATCHES "arm-none-eabi")
        include("${PNX_BOARD_TOOLCHAIN}")
    endif()
    unset(PNX_EMBEDDED_COMPILER_HINT)
endif()

project(pnx_embedded LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PNX_EMBEDDED_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configs")
set(PNX_EMBEDDED_GENERATED_DIR "${PNX_EMBEDDED_CONFIG_DIR}/generated")
set(PNX_EMBEDDED_ROBOT_CONFIG "${PNX_EMBEDDED_CONFIG_DIR}/robot.json")
set(PNX_EMBEDDED_BOARD_CONFIG "${PNX_BOARD_DIR}/board.json")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PNX_BOARD_IOC}"
    "${PNX_EMBEDDED_BOARD_CONFIG}"
    "${PNX_BOARD_DIR}/board.cmake"
    "${PNX_EMBEDDED_CONFIG_DIR}/params.json"
    "${PNX_EMBEDDED_ROBOT_CONFIG}"
    "${PNX_EMBEDDED_CONFIG_DIR}/cmake/generate_config.cmake"
    "${PNX_EMBEDDED_CONFIG_DIR}/cmake/import_ioc.cmake"
)

set(IOC "${PNX_BOARD_IOC}")
set(BOARD_CONFIG "${PNX_EMBEDDED_BOARD_CONFIG}")
set(PARAMS "${PNX_EMBEDDED_CONFIG_DIR}/params.json")
set(ROBOT_CONFIG "${PNX_EMBEDDED_ROBOT_CONFIG}")
set(OUT_DIR "${PNX_EMBEDDED_GENERATED_DIR}")
include("${PNX_EMBEDDED_CONFIG_DIR}/cmake/generate_config.cmake")
unset(IOC)
unset(BOARD_CONFIG)
unset(PARAMS)
unset(ROBOT_CONFIG)
unset(OUT_DIR)

file(GLOB_RECURSE PNX_EMBEDDED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_bsp/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_bsp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_devices/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_modules/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_libs/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/diagnose/*.cpp"
)

if(NOT ENABLE_USBX)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_bsp[/\\\\]usb[/\\\\]")
endif()

if(NOT HAS_DMIMU)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]imu[/\\\\]dmimu[/\\\\]")
endif()

if(NOT MOTOR_DJI)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]motors[/\\\\]dji[/\\\\]")
endif()
if(NOT MOTOR_DM)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]motors[/\\\\]dm[/\\\\]")
endif()
if(NOT MOTOR_LK)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]motors[/\\\\]lk[/\\\\]")
endif()
if(NOT MOTOR_XV2)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]motors[/\\\\]xv2[/\\\\]")
endif()

if(NOT ENABLE_DR16)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_modules[/\\\\]remoter[/\\\\]src[/\\\\]dr16\\.cpp$")
endif()
if(NOT ENABLE_VT03)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_modules[/\\\\]remoter[/\\\\]src[/\\\\]vt03\\.cpp$")
endif()
if(NOT ENABLE_PS2)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_modules[/\\\\]remoter[/\\\\]src[/\\\\]ps2\\.cpp$")
endif()
if(NOT ENABLE_PS2_UART)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_modules[/\\\\]remoter[/\\\\]src[/\\\\]ps2_uart\\.cpp$")
endif()

file(GLOB_RECURSE PNX_EMBEDDED_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_bsp/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_bsp/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_devices/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_devices/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_modules/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_modules/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_libs/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_libs/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/diagnose/*.hpp"
)

if(NOT HAS_DMIMU)
    list(FILTER PNX_EMBEDDED_HEADERS EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]imu[/\\\\]dmimu[/\\\\]")
endif()

set(PNX_EMBEDDED_INCLUDE_DIRS)
foreach(header IN LISTS PNX_EMBEDDED_HEADERS)
    get_filename_component(header_dir "${header}" DIRECTORY)
    list(APPEND PNX_EMBEDDED_INCLUDE_DIRS "${header_dir}")
endforeach()
list(REMOVE_DUPLICATES PNX_EMBEDDED_INCLUDE_DIRS)

set(CMSIS_DSP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Source/BasicMathFunctions/arm_dot_prod_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Source/MatrixFunctions/arm_mat_add_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Source/MatrixFunctions/arm_mat_init_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Source/MatrixFunctions/arm_mat_inverse_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Source/MatrixFunctions/arm_mat_mult_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Source/MatrixFunctions/arm_mat_sub_f32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Source/MatrixFunctions/arm_mat_trans_f32.c"
)

add_executable(${PROJECT_NAME})

add_subdirectory("${PNX_BOARD_CUBEMX_DIR}" "${CMAKE_BINARY_DIR}/stm32cubemx")

target_sources(${PROJECT_NAME} PRIVATE
    ${PNX_EMBEDDED_SOURCES}
    ${CMSIS_DSP_SOURCES}
    "${PNX_EMBEDDED_GENERATED_DIR}/bsp_bindings.cpp"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${PNX_EMBEDDED_INCLUDE_DIRS}
    "${CMAKE_CURRENT_SOURCE_DIR}/cmsis-dsp/Include"
    "${PNX_EMBEDDED_GENERATED_DIR}"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    PNX_DMA_DEDICATED_SECTION=${PNX_DMA_DEDICATED_SECTION}
    PNX_DMA_CACHE_LINE_SIZE=${PNX_DMA_CACHE_LINE_SIZE}
    ROBOT_MOTOR_COUNT=${robot_motor_count}
)

target_link_libraries(${PROJECT_NAME}
    stm32cubemx
)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)
