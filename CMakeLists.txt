cmake_minimum_required(VERSION 3.22)

set(PNX_BOARD "h723_mc02" CACHE STRING "Selected PNX board profile")
set_property(CACHE PNX_BOARD PROPERTY STRINGS h723_mc02 f407_c_board)
set(PNX_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/boards/${PNX_BOARD}")
if(NOT EXISTS "${PNX_BOARD_DIR}/board.cmake" OR NOT EXISTS "${PNX_BOARD_DIR}/board.json")
    message(FATAL_ERROR "Unknown or incomplete PNX_BOARD='${PNX_BOARD}'")
endif()
include("${PNX_BOARD_DIR}/board.cmake")

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake"
        CACHE FILEPATH "ARM toolchain for the selected board")
endif()

project(pnx_embedded LANGUAGES C CXX ASM)

# Debug tools follow the same board selection as the firmware.
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/pnx-openocd.cfg"
    CONTENT "source [find ${PNX_BOARD_OPENOCD_TARGET}]\n" @ONLY)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PNX_EMBEDDED_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/configs")
set(PNX_EMBEDDED_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
foreach(required_board_setting IN ITEMS
        PNX_BOARD_IOC
        PNX_BOARD_CUBEMX_DIR
        PNX_BOARD_TOOLCHAIN
        PNX_BOARD_LINKER_SCRIPT
        PNX_BOARD_PARAMS
        PNX_BOARD_ROBOT_CONFIG
        PNX_BOARD_FAMILY)
    if(NOT DEFINED ${required_board_setting})
        message(FATAL_ERROR
            "Board profile '${PNX_BOARD}' does not define ${required_board_setting}")
    endif()
endforeach()

foreach(required_board_path IN ITEMS
        PNX_BOARD_IOC
        PNX_BOARD_CUBEMX_DIR
        PNX_BOARD_TOOLCHAIN
        PNX_BOARD_LINKER_SCRIPT
        PNX_BOARD_PARAMS
        PNX_BOARD_ROBOT_CONFIG)
    if(NOT EXISTS "${${required_board_path}}")
        message(FATAL_ERROR
            "Board profile '${PNX_BOARD}' path ${required_board_path} does not exist: ${${required_board_path}}")
    endif()
endforeach()

set(PNX_BSP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/pnx_bsp" CACHE PATH
    "Development override for the matching pnx_bsp branch checkout")
mark_as_advanced(PNX_BSP_SOURCE_DIR)
get_filename_component(PNX_BSP_SOURCE_DIR "${PNX_BSP_SOURCE_DIR}" ABSOLUTE)

set(PNX_BSP_TARGET_FILE "${PNX_BSP_SOURCE_DIR}/bsp/target.cmake")
if(NOT EXISTS "${PNX_BSP_TARGET_FILE}")
    message(FATAL_ERROR
        "Selected BSP checkout has no target manifest: ${PNX_BSP_TARGET_FILE}")
endif()
unset(PNX_BSP_FAMILY)
include("${PNX_BSP_TARGET_FILE}")
if(NOT DEFINED PNX_BSP_FAMILY OR NOT PNX_BSP_FAMILY STREQUAL PNX_BOARD_FAMILY)
    message(FATAL_ERROR
        "PNX_BOARD='${PNX_BOARD}' requires BSP family '${PNX_BOARD_FAMILY}', "
        "but '${PNX_BSP_SOURCE_DIR}' provides '${PNX_BSP_FAMILY}'. "
        "Checkout the matching pnx_bsp V2 branch; PNX_BSP_SOURCE_DIR is only a development override.")
endif()
message(STATUS "PNX board: ${PNX_BOARD} (${PNX_BOARD_FAMILY}); BSP: ${PNX_BSP_SOURCE_DIR}")

set(PNX_ROBOT_CONFIG_OVERRIDE "" CACHE FILEPATH
    "Validation/development override for the selected board's robot configuration")
mark_as_advanced(PNX_ROBOT_CONFIG_OVERRIDE)
if(PNX_ROBOT_CONFIG_OVERRIDE)
    if(NOT EXISTS "${PNX_ROBOT_CONFIG_OVERRIDE}")
        message(FATAL_ERROR "PNX_ROBOT_CONFIG_OVERRIDE does not exist: ${PNX_ROBOT_CONFIG_OVERRIDE}")
    endif()
    get_filename_component(PNX_EMBEDDED_ROBOT_CONFIG "${PNX_ROBOT_CONFIG_OVERRIDE}" ABSOLUTE)
else()
    set(PNX_EMBEDDED_ROBOT_CONFIG "${PNX_BOARD_ROBOT_CONFIG}")
endif()
set(PNX_PARAMS_OVERRIDE "" CACHE FILEPATH "Validation/development params override")
mark_as_advanced(PNX_PARAMS_OVERRIDE)
if(PNX_PARAMS_OVERRIDE)
    get_filename_component(PNX_BOARD_PARAMS "${PNX_PARAMS_OVERRIDE}" ABSOLUTE)
endif()
set(PNX_EMBEDDED_BOARD_CONFIG "${PNX_BOARD_DIR}/board.json")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PNX_BOARD_IOC}"
    "${PNX_EMBEDDED_BOARD_CONFIG}"
    "${PNX_BOARD_DIR}/board.cmake"
    "${PNX_BOARD_PARAMS}"
    "${PNX_EMBEDDED_ROBOT_CONFIG}"
    "${PNX_EMBEDDED_CONFIG_DIR}/defaults.json"
    "${PNX_EMBEDDED_CONFIG_DIR}/cmake/generate_config.cmake"
    "${PNX_EMBEDDED_CONFIG_DIR}/cmake/import_ioc.cmake"
    "${PNX_BSP_TARGET_FILE}"
)

set(IOC "${PNX_BOARD_IOC}")
set(BOARD_CONFIG "${PNX_EMBEDDED_BOARD_CONFIG}")
set(PARAMS "${PNX_BOARD_PARAMS}")
set(ROBOT_CONFIG "${PNX_EMBEDDED_ROBOT_CONFIG}")
set(OUT_DIR "${PNX_EMBEDDED_GENERATED_DIR}")
include("${PNX_EMBEDDED_CONFIG_DIR}/cmake/generate_config.cmake")
unset(IOC)
unset(BOARD_CONFIG)
unset(PARAMS)
unset(ROBOT_CONFIG)
unset(OUT_DIR)

function(pnx_exclude_bsp_module source_list module_name)
    file(TO_CMAKE_PATH "${PNX_BSP_SOURCE_DIR}/${module_name}/" module_prefix)
    set(filtered_sources)
    foreach(source IN LISTS ${source_list})
        file(TO_CMAKE_PATH "${source}" normalized_source)
        string(FIND "${normalized_source}" "${module_prefix}" module_position)
        if(NOT module_position EQUAL 0)
            list(APPEND filtered_sources "${source}")
        endif()
    endforeach()
    set(${source_list} "${filtered_sources}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE PNX_EMBEDDED_SOURCES CONFIGURE_DEPENDS
    "${PNX_BSP_SOURCE_DIR}/*.c"
    "${PNX_BSP_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_devices/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_modules/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_libs/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/diagnose/*.cpp"
)

if(NOT ENABLE_USART_TEST)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "/diagnose/usart/")
endif()

if(NOT ENABLE_USBX)
    pnx_exclude_bsp_module(PNX_EMBEDDED_SOURCES usb)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]diagnose[/\\\\]usb[/\\\\]")
endif()

if(NOT HAS_DMIMU)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]imu[/\\\\]dmimu[/\\\\]")
endif()

# Driver families share the same directory convention; keep selection explicit.
foreach(family IN ITEMS DJI DM LK XV2)
    if(NOT MOTOR_${family})
        string(TOLOWER "${family}" directory)
        list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "/pnx_devices/motors/${directory}/")
    endif()
endforeach()

foreach(protocol IN ITEMS DR16 VT03 PS2 PS2_UART)
    if(NOT ENABLE_${protocol})
        string(TOLOWER "${protocol}" filename)
        list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "/pnx_modules/remoter/src/${filename}\\.cpp$")
    endif()
endforeach()
if(NOT ENABLE_PS2)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "/diagnose/ps2/")
endif()

if(NOT ENABLE_GPIO_TEST)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]diagnose[/\\\\]gpio[/\\\\]")
endif()

if(robot_motor_count EQUAL 0)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]motors[/\\\\]")
endif()

if(ENABLE_MOTOR_TEST AND robot_motor_count EQUAL 0)
    message(FATAL_ERROR "test.motor_demo requires configured motors")
endif()
if(NOT ENABLE_MOTOR_TEST)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]diagnose[/\\\\]motor[/\\\\]")
endif()

# The LED device is the SPI WS2812 consumer. GPIO indicators use BSP GPIO.
if(NOT HAS_LED)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "/pnx_devices/led/")
endif()

if(NOT HAS_BMI088)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]imu[/\\\\]bmi088[/\\\\]")
endif()

if(NOT HAS_AHRS AND NOT HAS_DMIMU)
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]pnx_modules[/\\\\]ahrs[/\\\\]")
    list(FILTER PNX_EMBEDDED_SOURCES EXCLUDE REGEX "[/\\\\]diagnose[/\\\\]imu[/\\\\]")
endif()

if(pwm_channel_count EQUAL 0)
    pnx_exclude_bsp_module(PNX_EMBEDDED_SOURCES pwm)
    pnx_exclude_bsp_module(PNX_EMBEDDED_SOURCES pwm_failsafe)
endif()

if(spi_bus_count EQUAL 0)
    pnx_exclude_bsp_module(PNX_EMBEDDED_SOURCES spi)
endif()

if(adc_channel_count EQUAL 0)
    pnx_exclude_bsp_module(PNX_EMBEDDED_SOURCES adc)
endif()

file(GLOB_RECURSE PNX_EMBEDDED_HEADERS CONFIGURE_DEPENDS
    "${PNX_BSP_SOURCE_DIR}/*.h"
    "${PNX_BSP_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_devices/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_devices/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_modules/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_modules/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_libs/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/pnx_libs/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/diagnose/*.hpp"
)

if(NOT HAS_DMIMU)
    list(FILTER PNX_EMBEDDED_HEADERS EXCLUDE REGEX "[/\\\\]pnx_devices[/\\\\]imu[/\\\\]dmimu[/\\\\]")
endif()

set(PNX_EMBEDDED_INCLUDE_DIRS)
set(PNX_BSP_PRIVATE_INCLUDE_DIRS)
foreach(header IN LISTS PNX_EMBEDDED_HEADERS)
    get_filename_component(header_dir "${header}" DIRECTORY)
    cmake_path(IS_PREFIX PNX_BSP_SOURCE_DIR "${header}" NORMALIZE is_bsp_header)
    if(is_bsp_header AND header_dir MATCHES "/src$")
        list(APPEND PNX_BSP_PRIVATE_INCLUDE_DIRS "${header_dir}")
    else()
        list(APPEND PNX_EMBEDDED_INCLUDE_DIRS "${header_dir}")
    endif()
endforeach()
list(REMOVE_DUPLICATES PNX_EMBEDDED_INCLUDE_DIRS)
list(REMOVE_DUPLICATES PNX_BSP_PRIVATE_INCLUDE_DIRS)
foreach(source IN LISTS PNX_EMBEDDED_SOURCES)
    cmake_path(IS_PREFIX PNX_BSP_SOURCE_DIR "${source}" NORMALIZE is_bsp_source)
    if(is_bsp_source)
        set_property(SOURCE "${source}" APPEND PROPERTY
            INCLUDE_DIRECTORIES "${PNX_BSP_PRIVATE_INCLUDE_DIRS}")
    endif()
endforeach()
set_property(SOURCE "${PNX_EMBEDDED_GENERATED_DIR}/bsp_bindings.cpp" APPEND PROPERTY
    INCLUDE_DIRECTORIES "${PNX_BSP_PRIVATE_INCLUDE_DIRS}")

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
if(PNX_BOARD_FAMILY STREQUAL "stm32f4")
    # F4 tx_application_define registers a stack-error callback before app startup.
    # ThreadX returns TX_FEATURE_NOT_ENABLED unless this is enabled consistently.
    target_compile_definitions(stm32cubemx INTERFACE TX_ENABLE_STACK_CHECKING)
endif()
# CubeMX Core owns ThreadX entry; its USER CODE hook calls app_start.
include("${PNX_EMBEDDED_CONFIG_DIR}/cmake/usbx_integration.cmake")

target_sources(${PROJECT_NAME} PRIVATE
    ${PNX_EMBEDDED_SOURCES}
    ${CMSIS_DSP_SOURCES}
    "${PNX_EMBEDDED_GENERATED_DIR}/bsp_bindings.cpp"
)

# Keep the build-local product source selection inspectable without turning the
# source tree into a second generated-configuration location.
set(PNX_EMBEDDED_SOURCE_MANIFEST "${CMAKE_BINARY_DIR}/pnx_embedded_sources.txt")
list(JOIN PNX_EMBEDDED_SOURCES "\n" PNX_EMBEDDED_SOURCE_MANIFEST_BODY)
list(JOIN CMSIS_DSP_SOURCES "\n" PNX_EMBEDDED_CMSIS_SOURCE_MANIFEST_BODY)
string(CONCAT pnx_source_manifest_content
    "# Sources selected directly by the PNX product target. Generated during configure.\n"
    "${PNX_EMBEDDED_SOURCE_MANIFEST_BODY}\n"
    "${PNX_EMBEDDED_CMSIS_SOURCE_MANIFEST_BODY}\n"
    "${PNX_EMBEDDED_GENERATED_DIR}/bsp_bindings.cpp\n")
file(CONFIGURE OUTPUT "${PNX_EMBEDDED_SOURCE_MANIFEST}" CONTENT "${pnx_source_manifest_content}" @ONLY)
message(STATUS "Product source manifest: ${PNX_EMBEDDED_SOURCE_MANIFEST}")

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
