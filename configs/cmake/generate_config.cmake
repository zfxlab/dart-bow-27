cmake_minimum_required(VERSION 3.22)

include(${CMAKE_CURRENT_LIST_DIR}/import_ioc.cmake)

if(NOT DEFINED IOC OR NOT DEFINED BOARD_CONFIG OR NOT DEFINED PARAMS OR NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "generate_config.cmake requires IOC, BOARD_CONFIG, PARAMS and OUT_DIR")
endif()

pnx_ioc_parse("${IOC}")

file(READ "${PARAMS}" params_json)
file(READ "${BOARD_CONFIG}" board_json)
set(generated_semicolon_token "__PNX_GENERATED_SEMICOLON__")

# --- board.json: memory policy ---
string(JSON board_dma_policy GET "${board_json}" memory dma_policy)
string(TOLOWER "${board_dma_policy}" board_dma_policy)
if(board_dma_policy STREQUAL "dedicated_section")
    set(PNX_DMA_DEDICATED_SECTION 1)
elseif(board_dma_policy STREQUAL "default")
    set(PNX_DMA_DEDICATED_SECTION 0)
else()
    message(FATAL_ERROR "board memory.dma_policy must be dedicated_section or default")
endif()
string(JSON PNX_DMA_CACHE_LINE_SIZE GET "${board_json}" memory cache_line_size)
if(PNX_DMA_CACHE_LINE_SIZE LESS 1)
    message(FATAL_ERROR "board memory.cache_line_size must be positive")
endif()
math(EXPR dma_cache_line_mask "${PNX_DMA_CACHE_LINE_SIZE} - 1")
math(EXPR dma_cache_line_power_check "${PNX_DMA_CACHE_LINE_SIZE} & ${dma_cache_line_mask}")
if(NOT dma_cache_line_power_check EQUAL 0)
    message(FATAL_ERROR "board memory.cache_line_size must be a power of two")
endif()

set(dma_accessible_range_list "")
string(JSON dma_accessible_range_count ERROR_VARIABLE json_err LENGTH "${board_json}" memory dma_accessible_ranges)
if(json_err)
    set(dma_accessible_range_count 0)
endif()
if(dma_accessible_range_count GREATER 0)
    math(EXPR dma_accessible_range_last "${dma_accessible_range_count} - 1")
    foreach(index RANGE 0 ${dma_accessible_range_last})
        string(JSON dma_range_start GET "${board_json}" memory dma_accessible_ranges ${index} start)
        string(JSON dma_range_end GET "${board_json}" memory dma_accessible_ranges ${index} end)
        math(EXPR dma_range_start_value "${dma_range_start}")
        math(EXPR dma_range_end_value "${dma_range_end}")
        if(dma_range_end_value LESS_EQUAL dma_range_start_value)
            message(FATAL_ERROR "board DMA accessible range ${index} has an invalid end")
        endif()
        list(APPEND dma_accessible_range_list "{ ${dma_range_start}UL, ${dma_range_end}UL }")
    endforeach()
endif()
list(JOIN dma_accessible_range_list ", " dma_accessible_ranges_cpp)

# --- params.json: build ---
string(JSON build_usbx ERROR_VARIABLE json_err GET "${params_json}" build usbx)
if(json_err)
    set(build_usbx "false")
endif()
pnx_to_json_bool("${build_usbx}" _usbx_json_unused)
if(build_usbx STREQUAL "true" OR build_usbx STREQUAL "1" OR build_usbx STREQUAL "ON")
    set(params_usbx TRUE)
else()
    set(params_usbx FALSE)
endif()

function(_pnx_json_bool_to_cmake val out_var)
    if(val STREQUAL "true" OR val STREQUAL "1" OR val STREQUAL "ON")
        set(${out_var} ON PARENT_SCOPE)
    else()
        set(${out_var} OFF PARENT_SCOPE)
    endif()
endfunction()

function(_pnx_can_id_type_expr val out_var)
    string(TOLOWER "${val}" val_lower)
    if(val_lower STREQUAL "standard" OR val_lower STREQUAL "std")
        set(${out_var} "id_type::standard" PARENT_SCOPE)
    elseif(val_lower STREQUAL "extended" OR val_lower STREQUAL "ext")
        set(${out_var} "id_type::extended" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "can id_type must be standard or extended")
    endif()
endfunction()

function(_pnx_can_header_literal value role direction max_id out_var)
    math(EXPR header_value "${value}")
    if(header_value LESS 0 OR header_value GREATER max_id)
        message(FATAL_ERROR
            "CAN role '${role}' ${direction}_header ${value} is outside the configured CAN ID range")
    endif()
    set(${out_var} "${header_value}U" PARENT_SCOPE)
endfunction()

function(_pnx_cpp_identifier input out_var)
    string(REGEX REPLACE "[^A-Za-z0-9_]" "_" ident "${input}")
    string(REGEX REPLACE "_+" "_" ident "${ident}")
    string(REGEX REPLACE "^_+|_+$" "" ident "${ident}")
    if(ident STREQUAL "")
        set(ident "unnamed")
    endif()
    if(ident MATCHES "^[0-9]")
        set(ident "_${ident}")
    endif()
    set(${out_var} "${ident}" PARENT_SCOPE)
endfunction()

# --- robot.json: optional DMIMU build switch ---
# Absence of devices.dmimu, or absence/false value of its enabled member,
# deliberately disables DMIMU. This keeps legacy robot files opt-in.
set(robot_json "")
set(HAS_DMIMU 0)
if(DEFINED ROBOT_CONFIG AND EXISTS "${ROBOT_CONFIG}")
    file(READ "${ROBOT_CONFIG}" robot_json)
    string(JSON robot_dmimu_type ERROR_VARIABLE json_err TYPE "${robot_json}" devices dmimu)
    if(NOT json_err)
        if(NOT robot_dmimu_type STREQUAL "OBJECT")
            message(FATAL_ERROR "robot devices.dmimu must be an object")
        endif()
        string(JSON robot_dmimu_enabled_type ERROR_VARIABLE json_err TYPE "${robot_json}" devices dmimu enabled)
        if(NOT json_err AND NOT robot_dmimu_enabled_type STREQUAL "BOOLEAN")
            message(FATAL_ERROR "robot devices.dmimu.enabled must be a boolean")
        endif()
        string(JSON robot_dmimu_enabled ERROR_VARIABLE json_err GET "${robot_json}" devices dmimu enabled)
        if(NOT json_err)
            _pnx_json_bool_to_cmake("${robot_dmimu_enabled}" robot_dmimu_enabled_cmake)
            if(robot_dmimu_enabled_cmake)
                set(HAS_DMIMU 1)
            endif()
        endif()
    endif()
endif()

# Motor protocol compilation follows the configured robot models. A separate
# availability list would create two sources of truth.
set(MOTOR_DJI OFF)
set(MOTOR_DM OFF)
set(MOTOR_LK OFF)
set(MOTOR_XV2 OFF)
if(NOT robot_json STREQUAL "")
    string(JSON build_motor_count ERROR_VARIABLE json_err LENGTH "${robot_json}" devices motors list)
    if(NOT json_err AND build_motor_count GREATER 0)
        math(EXPR build_motor_last "${build_motor_count} - 1")
        foreach(index RANGE 0 ${build_motor_last})
            string(JSON build_motor_model GET "${robot_json}" devices motors list ${index} model)
            string(TOLOWER "${build_motor_model}" build_motor_model)
            if(build_motor_model MATCHES "^dji_")
                set(MOTOR_DJI ON)
            elseif(build_motor_model MATCHES "^dm_")
                set(MOTOR_DM ON)
            elseif(build_motor_model MATCHES "^lk_")
                set(MOTOR_LK ON)
            elseif(build_motor_model MATCHES "^xv2_")
                set(MOTOR_XV2 ON)
            endif()
        endforeach()
    endif()
endif()

# --- params.json: bindings ---
string(JSON remoter_uart ERROR_VARIABLE json_err GET "${params_json}" bindings remoter_uart)
if(json_err)
    set(remoter_uart "uart5")
endif()
string(JSON referee_uart ERROR_VARIABLE json_err GET "${params_json}" bindings referee_uart)
if(json_err)
    set(referee_uart "usart1")
endif()
string(JSON remoter_source ERROR_VARIABLE json_err GET "${params_json}" remoter source)
if(json_err)
    set(remoter_source "")
endif()
string(TOLOWER "${remoter_uart}" remoter_uart)
string(TOLOWER "${referee_uart}" referee_uart)
string(TOLOWER "${remoter_source}" remoter_source)

set(gpio_input_config_list "")
set(gpio_input_enum_entries "")
set(gpio_input_binding_body "")
set(gpio_output_config_list "")
set(gpio_output_enum_entries "")
set(gpio_output_binding_body "")

string(JSON gpio_input_count ERROR_VARIABLE json_err LENGTH "${board_json}" bindings gpio_inputs)
if(json_err)
    set(gpio_input_count 0)
endif()
if(gpio_input_count GREATER 0)
    math(EXPR gpio_input_last "${gpio_input_count} - 1")
    foreach(index RANGE 0 ${gpio_input_last})
        string(JSON role MEMBER "${board_json}" bindings gpio_inputs ${index})
        _pnx_cpp_identifier("${role}" role_ident)
        if(NOT role_ident STREQUAL role)
            message(FATAL_ERROR "GPIO input role '${role}' must be a C++ identifier")
        endif()
        string(JSON pin GET "${board_json}" bindings gpio_inputs ${role} pin)
        string(JSON active_level GET "${board_json}" bindings gpio_inputs ${role} active_level)
        string(TOLOWER "${pin}" pin)
        string(TOLOWER "${active_level}" active_level)
        if(NOT pin MATCHES "^p([a-k])([0-9]|1[0-5])$")
            message(FATAL_ERROR "GPIO input role '${role}' has invalid pin '${pin}'")
        endif()
        set(port "${CMAKE_MATCH_1}")
        set(pin_number "${CMAKE_MATCH_2}")
        if(NOT active_level STREQUAL "low" AND NOT active_level STREQUAL "high")
            message(FATAL_ERROR "GPIO input role '${role}' active_level must be low or high")
        endif()
        string(TOUPPER "${pin}" pin_upper)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${pin_upper}.Signal" signal)
        if(signal STREQUAL "")
            pnx_ioc_get_value("${PNX_IOC_LINES}" "${pin_upper}_C.Signal" signal)
        endif()
        if(NOT signal STREQUAL "GPIO_Input" AND NOT signal MATCHES "^GPXTI[0-9]+$")
            message(FATAL_ERROR "GPIO input role '${role}' pin ${pin} is not an IOC input")
        endif()
        list(APPEND gpio_input_config_list
            "{ port_id::${port}, ${pin_number}U, active_level::${active_level} }")
        if(NOT gpio_input_enum_entries STREQUAL "")
            string(APPEND gpio_input_enum_entries ", ")
        endif()
        string(APPEND gpio_input_enum_entries "${role_ident} = ${index}")
        string(APPEND gpio_input_binding_body
            "inline constexpr bsp::gpio::input ${role_ident} = bsp::gpio::input::${role_ident}${generated_semicolon_token}\n")
    endforeach()
endif()

string(JSON gpio_output_count ERROR_VARIABLE json_err LENGTH "${board_json}" bindings gpio_outputs)
if(json_err)
    set(gpio_output_count 0)
endif()
if(gpio_output_count GREATER 0)
    math(EXPR gpio_output_last "${gpio_output_count} - 1")
    foreach(index RANGE 0 ${gpio_output_last})
        string(JSON role MEMBER "${board_json}" bindings gpio_outputs ${index})
        _pnx_cpp_identifier("${role}" role_ident)
        if(NOT role_ident STREQUAL role)
            message(FATAL_ERROR "GPIO output role '${role}' must be a C++ identifier")
        endif()
        string(JSON pin GET "${board_json}" bindings gpio_outputs ${role} pin)
        string(JSON active_level GET "${board_json}" bindings gpio_outputs ${role} active_level)
        string(TOLOWER "${pin}" pin)
        string(TOLOWER "${active_level}" active_level)
        if(NOT pin MATCHES "^p([a-k])([0-9]|1[0-5])$")
            message(FATAL_ERROR "GPIO output role '${role}' has invalid pin '${pin}'")
        endif()
        set(port "${CMAKE_MATCH_1}")
        set(pin_number "${CMAKE_MATCH_2}")
        if(NOT active_level STREQUAL "low" AND NOT active_level STREQUAL "high")
            message(FATAL_ERROR "GPIO output role '${role}' active_level must be low or high")
        endif()
        string(TOUPPER "${pin}" pin_upper)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${pin_upper}.Signal" signal)
        if(signal STREQUAL "")
            pnx_ioc_get_value("${PNX_IOC_LINES}" "${pin_upper}_C.Signal" signal)
        endif()
        if(NOT signal STREQUAL "GPIO_Output")
            message(FATAL_ERROR "GPIO output role '${role}' pin ${pin} is not an IOC output")
        endif()
        list(APPEND gpio_output_config_list
            "{ port_id::${port}, ${pin_number}U, active_level::${active_level} }")
        if(NOT gpio_output_enum_entries STREQUAL "")
            string(APPEND gpio_output_enum_entries ", ")
        endif()
        string(APPEND gpio_output_enum_entries "${role_ident} = ${index}")
        string(APPEND gpio_output_binding_body
            "inline constexpr bsp::gpio::output ${role_ident} = bsp::gpio::output::${role_ident}${generated_semicolon_token}\n")
    endforeach()
endif()
# --- Board device bindings and feature availability ---
string(JSON board_bmi088_enabled ERROR_VARIABLE json_err GET "${board_json}" devices bmi088 enabled)
if(json_err)
    set(board_bmi088_enabled "false")
endif()
_pnx_json_bool_to_cmake("${board_bmi088_enabled}" BOARD_HAS_BMI088)

set(bmi088_spi "")
set(bmi088_acc_cs "")
set(bmi088_gyro_cs "")
set(bmi088_gyro_drdy "")
set(bmi088_heater_pwm "")
set(HAS_BMI088_HEATER 0)
if(BOARD_HAS_BMI088)
    string(JSON bmi088_spi GET "${board_json}" devices bmi088 spi)
    string(JSON bmi088_acc_cs GET "${board_json}" devices bmi088 acc_cs)
    string(JSON bmi088_gyro_cs GET "${board_json}" devices bmi088 gyro_cs)
    string(JSON bmi088_gyro_drdy GET "${board_json}" devices bmi088 gyro_drdy)
    string(JSON bmi088_heater_pwm ERROR_VARIABLE json_err GET "${board_json}" devices bmi088 heater_pwm)
    if(json_err)
        set(bmi088_heater_pwm "")
    else()
        set(HAS_BMI088_HEATER 1)
        string(JSON bmi088_heater_timer GET "${board_json}" bindings pwm_channels ${bmi088_heater_pwm} timer)
        string(JSON bmi088_heater_channel GET "${board_json}" bindings pwm_channels ${bmi088_heater_pwm} channel)
        string(TOLOWER "${bmi088_heater_timer}_ch${bmi088_heater_channel}" bmi088_heater_channel_ident)
    endif()
    string(TOLOWER "${bmi088_spi}" bmi088_spi)
    pnx_ioc_hw_in_list("${PNX_IOC_SPI_HW}" "${bmi088_spi}" bmi088_spi_present)
    if(NOT bmi088_spi_present)
        message(FATAL_ERROR "board BMI088 binding requires ${bmi088_spi}, which is absent from the IOC")
    endif()
    set(HAS_AHRS 1)
else()
    set(HAS_AHRS 0)
endif()

string(JSON board_led_enabled ERROR_VARIABLE json_err GET "${board_json}" devices led enabled)
if(json_err)
    set(board_led_enabled "false")
endif()
_pnx_json_bool_to_cmake("${board_led_enabled}" BOARD_HAS_LED)
set(led_spi "")
if(BOARD_HAS_LED)
    string(JSON led_spi GET "${board_json}" devices led spi)
    string(TOLOWER "${led_spi}" led_spi)
    pnx_ioc_hw_in_list("${PNX_IOC_SPI_HW}" "${led_spi}" led_spi_present)
    if(NOT led_spi_present)
        message(FATAL_ERROR "board LED binding requires ${led_spi}, which is absent from the IOC")
    endif()
    set(HAS_LED 1)
else()
    set(HAS_LED 0)
endif()

# PS2 uses params.json for its compile-time backend and pins. The IOC remains
# authoritative for the GPIO directions and SPI peripheral configuration.
set(HAS_PS2_DEVICE 0)
set(PS2_BACKEND_SPI 0)
set(PS2_BACKEND_GPIO 0)
set(ps2_spi "")
set(ps2_cmd "ps2_cmd")
set(ps2_data "ps2_data")
set(ps2_clk "ps2_clk")
set(ps2_cs "ps2_cs")
string(JSON params_ps2_enabled ERROR_VARIABLE json_err GET "${params_json}" ps2 enabled)
if(json_err)
    set(params_ps2_enabled "false")
endif()
_pnx_json_bool_to_cmake("${params_ps2_enabled}" PARAMS_HAS_PS2_DEVICE)
if(PARAMS_HAS_PS2_DEVICE)
    string(JSON ps2_backend ERROR_VARIABLE json_err GET "${params_json}" ps2 backend)
    if(json_err)
        message(FATAL_ERROR "params.ps2 requires backend (spi or gpio)")
    endif()
    string(TOLOWER "${ps2_backend}" ps2_backend)

    if(ps2_backend STREQUAL "spi")
        string(JSON ps2_spi GET "${params_json}" ps2 spi)
        string(TOLOWER "${ps2_spi}" ps2_spi)
        pnx_ioc_hw_in_list("${PNX_IOC_SPI_HW}" "${ps2_spi}" ps2_spi_present)
        if(NOT ps2_spi_present)
            message(FATAL_ERROR "params.ps2 SPI binding requires ${ps2_spi}, which is absent from the IOC")
        endif()
        string(TOUPPER "${ps2_spi}" ps2_spi_upper)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_spi_upper}.Mode" ps2_spi_mode)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_spi_upper}.Direction" ps2_spi_direction)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_spi_upper}.CLKPolarity" ps2_spi_polarity)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_spi_upper}.CLKPhase" ps2_spi_phase)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_spi_upper}.FirstBit" ps2_spi_first_bit)
        if(NOT ps2_spi_mode STREQUAL "SPI_MODE_MASTER" OR
           NOT ps2_spi_direction STREQUAL "SPI_DIRECTION_2LINES" OR
           NOT ps2_spi_polarity STREQUAL "SPI_POLARITY_HIGH" OR
           NOT ps2_spi_phase STREQUAL "SPI_PHASE_2EDGE" OR
           NOT ps2_spi_first_bit STREQUAL "SPI_FIRSTBIT_LSB")
            message(FATAL_ERROR
                "params.ps2 SPI ${ps2_spi} must be CubeMX Master, 2-line, Mode 3, LSB first")
        endif()
        set(PS2_BACKEND_SPI 1)
    elseif(ps2_backend STREQUAL "gpio")
        set(PS2_BACKEND_GPIO 1)
    else()
        message(FATAL_ERROR "params.ps2.backend must be spi or gpio")
    endif()

    # CS is software GPIO in both backends. CMD/CLK/DATA are additionally
    # generated for the GPIO backend. These semantic roles intentionally do
    # not live in board.json.
    set(ps2_output_names cs)
    if(PS2_BACKEND_GPIO)
        list(APPEND ps2_output_names cmd clk)
    endif()
    foreach(ps2_output_name IN LISTS ps2_output_names)
        string(JSON ps2_output_pin ERROR_VARIABLE json_err GET "${params_json}" ps2 ${ps2_output_name})
        if(json_err)
            message(FATAL_ERROR "params.ps2.${ps2_output_name} must name a GPIO pin")
        endif()
        string(TOLOWER "${ps2_output_pin}" ps2_output_pin)
        if(NOT ps2_output_pin MATCHES "^p([a-k])([0-9]|1[0-5])$")
            message(FATAL_ERROR "params.ps2.${ps2_output_name} has invalid pin '${ps2_output_pin}'")
        endif()
        set(ps2_output_port "${CMAKE_MATCH_1}")
        set(ps2_output_number "${CMAKE_MATCH_2}")
        string(TOUPPER "${ps2_output_pin}" ps2_output_pin_upper)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_output_pin_upper}.Signal" ps2_output_signal)
        if(ps2_output_signal STREQUAL "")
            pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_output_pin_upper}_C.Signal" ps2_output_signal)
        endif()
        if(NOT ps2_output_signal STREQUAL "GPIO_Output")
            message(FATAL_ERROR "params.ps2.${ps2_output_name} pin ${ps2_output_pin} is not an IOC output")
        endif()
        if(ps2_output_name STREQUAL "cs")
            set(ps2_output_active_level low)
        else()
            set(ps2_output_active_level high)
        endif()
        set(ps2_output_role "ps2_${ps2_output_name}")
        list(LENGTH gpio_output_config_list ps2_output_index)
        list(APPEND gpio_output_config_list
            "{ port_id::${ps2_output_port}, ${ps2_output_number}U, active_level::${ps2_output_active_level} }")
        if(NOT gpio_output_enum_entries STREQUAL "")
            string(APPEND gpio_output_enum_entries ", ")
        endif()
        string(APPEND gpio_output_enum_entries "${ps2_output_role} = ${ps2_output_index}")
        string(APPEND gpio_output_binding_body
            "inline constexpr bsp::gpio::output ${ps2_output_role} = bsp::gpio::output::${ps2_output_role}${generated_semicolon_token}\n")
    endforeach()

    if(PS2_BACKEND_GPIO)
        string(JSON ps2_data_pin ERROR_VARIABLE json_err GET "${params_json}" ps2 data)
        if(json_err)
            message(FATAL_ERROR "params.ps2.data must name a GPIO pin")
        endif()
        string(TOLOWER "${ps2_data_pin}" ps2_data_pin)
        if(NOT ps2_data_pin MATCHES "^p([a-k])([0-9]|1[0-5])$")
            message(FATAL_ERROR "params.ps2.data has invalid pin '${ps2_data_pin}'")
        endif()
        set(ps2_data_port "${CMAKE_MATCH_1}")
        set(ps2_data_number "${CMAKE_MATCH_2}")
        string(TOUPPER "${ps2_data_pin}" ps2_data_pin_upper)
        pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_data_pin_upper}.Signal" ps2_data_signal)
        if(ps2_data_signal STREQUAL "")
            pnx_ioc_get_value("${PNX_IOC_LINES}" "${ps2_data_pin_upper}_C.Signal" ps2_data_signal)
        endif()
        if(NOT ps2_data_signal STREQUAL "GPIO_Input")
            message(FATAL_ERROR "params.ps2.data pin ${ps2_data_pin} is not an IOC input")
        endif()
        list(LENGTH gpio_input_config_list ps2_data_index)
        list(APPEND gpio_input_config_list
            "{ port_id::${ps2_data_port}, ${ps2_data_number}U, active_level::high }")
        if(NOT gpio_input_enum_entries STREQUAL "")
            string(APPEND gpio_input_enum_entries ", ")
        endif()
        string(APPEND gpio_input_enum_entries "ps2_data = ${ps2_data_index}")
        string(APPEND gpio_input_binding_body
            "inline constexpr bsp::gpio::input ps2_data = bsp::gpio::input::ps2_data${generated_semicolon_token}\n")
    endif()
    set(HAS_PS2_DEVICE 1)
endif()

list(JOIN gpio_input_config_list ", " gpio_input_config_cpp)
list(JOIN gpio_output_config_list ", " gpio_output_config_cpp)
list(LENGTH gpio_input_config_list gpio_input_count)
list(LENGTH gpio_output_config_list gpio_output_count)

pnx_ioc_hw_in_list("${PNX_IOC_UART_HW}" "${remoter_uart}" remoter_uart_present)
pnx_ioc_uart_has_dma("${PNX_IOC_LINES}" "${remoter_uart}" "RX" remoter_has_rx_dma)
if(remoter_uart_present AND remoter_has_rx_dma)
    set(HAS_REMOTER 1)
else()
    set(HAS_REMOTER 0)
endif()
set(HAS_PS2_UART ${HAS_REMOTER})

pnx_ioc_hw_in_list("${PNX_IOC_UART_HW}" "uart7" vt03_uart_present)
pnx_ioc_uart_has_dma("${PNX_IOC_LINES}" "uart7" "RX" vt03_has_rx_dma)
if(vt03_uart_present AND vt03_has_rx_dma)
    set(HAS_VT03 1)
else()
    set(HAS_VT03 0)
endif()

pnx_ioc_hw_in_list("${PNX_IOC_UART_HW}" "${referee_uart}" referee_uart_present)
if(referee_uart_present)
    set(HAS_REFEREE 1)
else()
    set(HAS_REFEREE 0)
endif()

if(HAS_REFEREE)
    set(HAS_UI 1)
else()
    set(HAS_UI 0)
endif()

if(remoter_source STREQUAL "")
    if(HAS_REMOTER)
        set(ENABLE_DR16 1)
        set(ENABLE_VT03 0)
        set(ENABLE_PS2 0)
        set(ENABLE_PS2_UART 0)
    elseif(HAS_VT03)
        set(ENABLE_DR16 0)
        set(ENABLE_VT03 1)
        set(ENABLE_PS2 0)
        set(ENABLE_PS2_UART 0)
    else()
        set(ENABLE_DR16 0)
        set(ENABLE_VT03 0)
        set(ENABLE_PS2 0)
        set(ENABLE_PS2_UART 0)
    endif()
elseif(remoter_source STREQUAL "dr16")
    if(NOT HAS_REMOTER)
        message(FATAL_ERROR "params.remoter.source=dr16 requires remoter UART RX DMA support in board/board.ioc")
    endif()
    set(ENABLE_DR16 1)
    set(ENABLE_VT03 0)
    set(ENABLE_PS2 0)
    set(ENABLE_PS2_UART 0)
elseif(remoter_source STREQUAL "vt03")
    if(NOT HAS_VT03)
        message(FATAL_ERROR "params.remoter.source=vt03 requires UART7 RX DMA support in board/board.ioc")
    endif()
    set(ENABLE_DR16 0)
    set(ENABLE_VT03 1)
    set(ENABLE_PS2 0)
    set(ENABLE_PS2_UART 0)
elseif(remoter_source STREQUAL "ps2")
    if(NOT HAS_PS2_DEVICE)
        message(FATAL_ERROR "params.remoter.source=ps2 requires params.ps2.enabled and valid PS2 pins")
    endif()
    set(ENABLE_DR16 0)
    set(ENABLE_VT03 0)
    set(ENABLE_PS2 1)
    set(ENABLE_PS2_UART 0)
elseif(remoter_source STREQUAL "ps2_uart")
    if(NOT HAS_PS2_UART)
        message(FATAL_ERROR "params.remoter.source=ps2_uart requires the bound remoter UART to have RX DMA support in board/board.ioc")
    endif()
    set(ENABLE_DR16 0)
    set(ENABLE_VT03 0)
    set(ENABLE_PS2 0)
    set(ENABLE_PS2_UART 1)
else()
    message(FATAL_ERROR "params.remoter.source must be one of: dr16, vt03, ps2, ps2_uart")
endif()

list(LENGTH PNX_IOC_FDCAN_HW fdcan_count)
if(fdcan_count GREATER 0)
    set(HAS_MOTORS 1)
else()
    set(HAS_MOTORS 0)
endif()

string(JSON can_diag_enabled ERROR_VARIABLE json_err GET "${params_json}" can_diag enabled)
if(json_err OR can_diag_enabled STREQUAL "")
    set(can_diag_enabled "true")
endif()
if(can_diag_enabled STREQUAL "true" OR can_diag_enabled STREQUAL "1" OR can_diag_enabled STREQUAL "ON")
    set(CAN_DIAG_ENABLED 1)
else()
    set(CAN_DIAG_ENABLED 0)
endif()

if(PNX_IOC_HAS_USB AND params_usbx)
    set(ENABLE_USBX ON)
else()
    set(ENABLE_USBX OFF)
endif()

if(PNX_IOC_HAS_USB)
    set(HW_HAS_USB 1)
else()
    set(HW_HAS_USB 0)
endif()

if(ENABLE_USBX)
    set(ENABLE_USBX_C 1)
else()
    set(ENABLE_USBX_C 0)
endif()

# --- Board CAN policy ---
set(can_max_rx_callbacks 8)

set(can_enabled_list "")
set(can_type_list "")
set(can_id_type_list "")
set(can_config_list "")
set(can_bus_enum_entries "")
set(can_bus_index 0)

foreach(hw ${PNX_IOC_FDCAN_HW})
    string(TOLOWER "${hw}" hw_lower)

    list(APPEND can_enabled_list "true")

    pnx_ioc_fdcan_frame_format("${PNX_IOC_LINES}" "${hw_lower}" ioc_can_capability)
    if(ioc_can_capability STREQUAL "classic")
        set(can_capability_expr "bus_capability::classic")
        set(ioc_can_type "classic")
    elseif(ioc_can_capability STREQUAL "fd_no_brs")
        set(can_capability_expr "bus_capability::fd_no_brs")
        set(ioc_can_type "fd")
    elseif(ioc_can_capability STREQUAL "fd_brs")
        set(can_capability_expr "bus_capability::fd_brs")
        set(ioc_can_type "fd")
    else()
        message(FATAL_ERROR
            "IOC ${hw_lower}.FrameFormat must be FDCAN_FRAME_CLASSIC, FDCAN_FRAME_FD_NO_BRS, or FDCAN_FRAME_FD_BRS")
    endif()
    if(ioc_can_type STREQUAL "fd")
        set(can_type_expr "bus_type::fd")
    else()
        set(can_type_expr "bus_type::classic")
    endif()
    list(APPEND can_type_list "${can_type_expr}")

    string(JSON manual_can_id_type ERROR_VARIABLE json_err GET "${params_json}" can ${hw_lower} id_type)
    pnx_ioc_fdcan_std_filters("${PNX_IOC_LINES}" "${hw_lower}" std_filters)
    pnx_ioc_fdcan_ext_filters("${PNX_IOC_LINES}" "${hw_lower}" ext_filters)
    if(NOT json_err AND NOT manual_can_id_type STREQUAL "")
        _pnx_can_id_type_expr("${manual_can_id_type}" can_id_type_expr)
        if(can_id_type_expr STREQUAL "id_type::standard" AND std_filters LESS 1)
            message(FATAL_ERROR
                "params.can.${hw_lower}.id_type=standard requires at least one standard filter in the IOC")
        elseif(can_id_type_expr STREQUAL "id_type::extended" AND ext_filters LESS 1)
            message(FATAL_ERROR
                "params.can.${hw_lower}.id_type=extended requires at least one extended filter in the IOC")
        endif()
    else()
        if(std_filters GREATER 0)
            set(can_id_type_expr "id_type::standard")
        elseif(ext_filters GREATER 0)
            set(can_id_type_expr "id_type::extended")
        else()
            set(can_id_type_expr "id_type::standard")
        endif()
    endif()
    list(APPEND can_id_type_list "${can_id_type_expr}")

    list(APPEND can_config_list
        "{ true, handle_id::${hw_lower}, ${can_type_expr}, ${can_capability_expr}, ${can_id_type_expr} }")

    set("PNX_CAN_TYPE_${hw_lower}" "${ioc_can_type}")
    if(can_id_type_expr STREQUAL "id_type::extended")
        set("PNX_CAN_ID_TYPE_${hw_lower}" "extended")
    else()
        set("PNX_CAN_ID_TYPE_${hw_lower}" "standard")
    endif()

    if(can_bus_index GREATER 0)
        string(APPEND can_bus_enum_entries ", ")
    endif()
    string(APPEND can_bus_enum_entries "${hw_lower} = ${can_bus_index}")
    math(EXPR can_bus_index "${can_bus_index} + 1")
endforeach()

list(JOIN can_enabled_list ", " can_enabled_cpp)
list(JOIN can_type_list ", " can_type_cpp)
list(JOIN can_id_type_list ", " can_id_type_cpp)
list(JOIN can_config_list ", " can_config_cpp)
list(LENGTH PNX_IOC_FDCAN_HW can_bus_count)

# --- params.json: application CAN bindings ---
# A binding gives application code a stable semantic name for an IOC-enabled
# FDCAN instance. Pins, timing and frame format remain board/CubeMX concerns.
set(can_app_binding_body "")
string(JSON can_app_binding_count ERROR_VARIABLE json_err LENGTH "${params_json}" bindings can_buses)
if(json_err)
    set(can_app_binding_count 0)
endif()
if(can_app_binding_count GREATER 0)
    math(EXPR can_app_binding_last "${can_app_binding_count} - 1")
    foreach(index RANGE 0 ${can_app_binding_last})
        string(JSON role MEMBER "${params_json}" bindings can_buses ${index})
        _pnx_cpp_identifier("${role}" role_ident)
        if(NOT role_ident STREQUAL role)
            message(FATAL_ERROR "CAN role '${role}' must be a C++ identifier")
        endif()

        string(JSON can_binding_type ERROR_VARIABLE json_err TYPE "${params_json}" bindings can_buses ${role})
        if(json_err)
            message(FATAL_ERROR "CAN role '${role}' requires a binding value")
        endif()
        set(can_binding_rx_header "")
        set(can_binding_tx_header "")
        if(can_binding_type STREQUAL "STRING")
            # Legacy shorthand: the role specifies only its FDCAN instance.
            string(JSON can_binding_bus GET "${params_json}" bindings can_buses ${role})
        elseif(can_binding_type STREQUAL "OBJECT")
            string(JSON can_binding_bus_type ERROR_VARIABLE json_err TYPE "${params_json}" bindings can_buses ${role} bus)
            if(json_err OR NOT can_binding_bus_type STREQUAL "STRING")
                message(FATAL_ERROR "CAN role '${role}' requires string field bus")
            endif()
            string(JSON can_binding_bus GET "${params_json}" bindings can_buses ${role} bus)
            foreach(direction rx tx)
                string(JSON can_binding_header_type ERROR_VARIABLE json_err TYPE "${params_json}" bindings can_buses ${role} ${direction}_header)
                if(NOT json_err)
                    if(NOT can_binding_header_type STREQUAL "STRING" AND
                       NOT can_binding_header_type STREQUAL "NUMBER")
                        message(FATAL_ERROR
                            "CAN role '${role}' ${direction}_header must be a number or numeric string")
                    endif()
                    string(JSON can_binding_${direction}_header GET "${params_json}" bindings can_buses ${role} ${direction}_header)
                endif()
            endforeach()
        else()
            message(FATAL_ERROR "CAN role '${role}' must be a string or object")
        endif()
        if(can_binding_bus STREQUAL "")
            message(FATAL_ERROR "CAN role '${role}' requires an FDCAN instance name")
        endif()
        string(TOLOWER "${can_binding_bus}" can_binding_bus)
        pnx_ioc_hw_in_list("${PNX_IOC_FDCAN_HW}" "${can_binding_bus}" can_binding_bus_present)
        if(NOT can_binding_bus_present)
            message(FATAL_ERROR
                "CAN role '${role}' uses ${can_binding_bus}, which is not present in ${IOC}")
        endif()

        string(APPEND can_app_binding_body
            "inline constexpr bsp::can::bus ${role_ident} = bsp::can::bus::${can_binding_bus}${generated_semicolon_token}\n")
        set(can_binding_id_type_var "PNX_CAN_ID_TYPE_${can_binding_bus}")
        if("${${can_binding_id_type_var}}" STREQUAL "extended")
            set(can_binding_max_id 0x1FFFFFFF)
        else()
            set(can_binding_max_id 0x7FF)
        endif()
        if(NOT can_binding_rx_header STREQUAL "")
            _pnx_can_header_literal("${can_binding_rx_header}" "${role}" "rx" ${can_binding_max_id} can_binding_rx_header_cpp)
            string(APPEND can_app_binding_body
                "inline constexpr std::uint32_t ${role_ident}_rx_header = ${can_binding_rx_header_cpp}${generated_semicolon_token}\n")
        endif()
        if(NOT can_binding_tx_header STREQUAL "")
            _pnx_can_header_literal("${can_binding_tx_header}" "${role}" "tx" ${can_binding_max_id} can_binding_tx_header_cpp)
            string(APPEND can_app_binding_body
                "inline constexpr std::uint32_t ${role_ident}_tx_header = ${can_binding_tx_header_cpp}${generated_semicolon_token}\n")
        endif()
    endforeach()
endif()

set(usart_enabled_list "")
set(usart_config_list "")
set(usart_port_enum_entries "")
set(uart_binding_body "")
set(uart_app_binding_body "")
set(usart_port_index 0)

foreach(hw ${PNX_IOC_UART_HW})
    string(TOLOWER "${hw}" hw_lower)

    pnx_ioc_uart_has_dma("${PNX_IOC_LINES}" "${hw_lower}" "RX" has_rx_dma)
    pnx_ioc_uart_has_dma("${PNX_IOC_LINES}" "${hw_lower}" "TX" has_tx_dma)
    pnx_to_json_bool("${has_rx_dma}" has_rx_dma_cpp)
    pnx_to_json_bool("${has_tx_dma}" has_tx_dma_cpp)
    list(APPEND usart_enabled_list "true")
    list(APPEND usart_config_list "{ true, handle_id::${hw_lower}, ${has_rx_dma_cpp}, ${has_tx_dma_cpp} }")

    if(usart_port_index GREATER 0)
        string(APPEND uart_binding_body "\n")
    endif()
    string(APPEND uart_binding_body "inline constexpr bsp::usart::port ${hw_lower} = ${usart_port_index};")

    if(usart_port_index GREATER 0)
        string(APPEND usart_port_enum_entries ", ")
    endif()
    string(APPEND usart_port_enum_entries "${hw_lower} = ${usart_port_index}")
    math(EXPR usart_port_index "${usart_port_index} + 1")
endforeach()

list(JOIN usart_enabled_list ", " usart_enabled_cpp)
list(JOIN usart_config_list ", " usart_config_cpp)
list(LENGTH PNX_IOC_UART_HW usart_port_count)

# --- params.json: application UART bindings ---
set(uart_reserved_roles dr16 vt03 ps2_uart referee test_report)
foreach(hw ${PNX_IOC_UART_HW})
    string(TOLOWER "${hw}" hw_lower)
    list(APPEND uart_reserved_roles "${hw_lower}")
endforeach()
string(JSON uart_app_binding_count ERROR_VARIABLE json_err LENGTH "${params_json}" bindings uart_ports)
if(json_err)
    set(uart_app_binding_count 0)
endif()
if(uart_app_binding_count GREATER 0)
    math(EXPR uart_app_binding_last "${uart_app_binding_count} - 1")
    foreach(index RANGE 0 ${uart_app_binding_last})
        string(JSON role MEMBER "${params_json}" bindings uart_ports ${index})
        _pnx_cpp_identifier("${role}" role_ident)
        if(NOT role_ident STREQUAL role)
            message(FATAL_ERROR "UART role '${role}' must be a C++ identifier")
        endif()
        list(FIND uart_reserved_roles "${role_ident}" uart_reserved_role_index)
        if(NOT uart_reserved_role_index LESS 0)
            message(FATAL_ERROR "UART role '${role}' conflicts with a generated app::uart name")
        endif()

        string(JSON uart_binding_type ERROR_VARIABLE json_err TYPE "${params_json}" bindings uart_ports ${role})
        if(json_err OR NOT uart_binding_type STREQUAL "STRING")
            message(FATAL_ERROR "UART role '${role}' must name a UART instance")
        endif()
        string(JSON uart_binding_port GET "${params_json}" bindings uart_ports ${role})
        string(TOLOWER "${uart_binding_port}" uart_binding_port)
        pnx_ioc_uart_index("${PNX_IOC_UART_HW}" "${uart_binding_port}" uart_binding_port_index)
        if(uart_binding_port_index LESS 0)
            message(FATAL_ERROR
                "UART role '${role}' uses ${uart_binding_port}, which is not present in ${IOC}")
        endif()
        string(APPEND uart_app_binding_body
            "inline constexpr bsp::usart::port ${role_ident} = ${uart_binding_port_index}${generated_semicolon_token}\n")
    endforeach()
endif()

set(spi_config_list "")
set(spi_handle_enum_entries "none = 0")
set(spi_bus_enum_entries "")
set(spi_binding_cases "")
set(spi_bus_index 0)
foreach(hw ${PNX_IOC_SPI_HW})
    string(TOLOWER "${hw}" hw_lower)
    pnx_hw_to_handle("${hw_lower}" spi_handle)
    pnx_ioc_spi_has_irq("${PNX_IOC_LINES}" "${hw_lower}" spi_has_irq)
    pnx_ioc_spi_has_dma("${PNX_IOC_LINES}" "${hw_lower}" "RX" spi_has_rx_dma)
    pnx_ioc_spi_has_dma("${PNX_IOC_LINES}" "${hw_lower}" "TX" spi_has_tx_dma)
    pnx_to_json_bool("${spi_has_irq}" spi_has_irq_cpp)
    pnx_to_json_bool("${spi_has_rx_dma}" spi_has_rx_dma_cpp)
    pnx_to_json_bool("${spi_has_tx_dma}" spi_has_tx_dma_cpp)
    list(APPEND spi_config_list "{ true, handle_id::${hw_lower} }")
    string(APPEND spi_handle_enum_entries ", ${hw_lower}")
    if(spi_bus_index GREATER 0)
        string(APPEND spi_bus_enum_entries ", ")
    endif()
    string(APPEND spi_bus_enum_entries "${hw_lower} = ${spi_bus_index}")
    string(APPEND spi_binding_cases
        "    case bus::${hw_lower}: out = { ${spi_handle}, ${spi_has_irq_cpp}, ${spi_has_rx_dma_cpp}, ${spi_has_tx_dma_cpp} }${generated_semicolon_token} return true${generated_semicolon_token}\n")
    math(EXPR spi_bus_index "${spi_bus_index} + 1")
endforeach()
list(JOIN spi_config_list ", " spi_config_cpp)
list(LENGTH PNX_IOC_SPI_HW spi_bus_count)

# --- params.json: application SPI bindings ---
# A binding gives application code a stable semantic name for an IOC-enabled
# SPI instance. Board-owned devices continue to use board::device bindings.
set(spi_app_binding_body "")
string(JSON spi_app_binding_count ERROR_VARIABLE json_err LENGTH "${params_json}" bindings spi_buses)
if(json_err)
    set(spi_app_binding_count 0)
endif()
if(spi_app_binding_count GREATER 0)
    math(EXPR spi_app_binding_last "${spi_app_binding_count} - 1")
    foreach(index RANGE 0 ${spi_app_binding_last})
        string(JSON role MEMBER "${params_json}" bindings spi_buses ${index})
        _pnx_cpp_identifier("${role}" role_ident)
        if(NOT role_ident STREQUAL role)
            message(FATAL_ERROR "SPI role '${role}' must be a C++ identifier")
        endif()

        string(JSON spi_binding_type ERROR_VARIABLE json_err TYPE "${params_json}" bindings spi_buses ${role})
        if(json_err OR NOT spi_binding_type STREQUAL "STRING")
            message(FATAL_ERROR "SPI role '${role}' must name an SPI instance")
        endif()
        string(JSON spi_binding_bus GET "${params_json}" bindings spi_buses ${role})
        string(TOLOWER "${spi_binding_bus}" spi_binding_bus)
        pnx_ioc_hw_in_list("${PNX_IOC_SPI_HW}" "${spi_binding_bus}" spi_binding_bus_present)
        if(NOT spi_binding_bus_present)
            message(FATAL_ERROR
                "SPI role '${role}' uses ${spi_binding_bus}, which is not present in ${IOC}")
        endif()

        string(APPEND spi_app_binding_body
            "inline constexpr bsp::spi::bus ${role_ident} = bsp::spi::bus::${spi_binding_bus}${generated_semicolon_token}\n")
    endforeach()
endif()

set(board_device_binding_body "")
if(BOARD_HAS_BMI088)
    string(APPEND board_device_binding_body
        "namespace bmi088 {\n"
        "inline constexpr bsp::spi::bus spi = bsp::spi::bus::${bmi088_spi}${generated_semicolon_token}\n"
        "inline constexpr bsp::gpio::output acc_cs = bsp::gpio::output::${bmi088_acc_cs}${generated_semicolon_token}\n"
        "inline constexpr bsp::gpio::output gyro_cs = bsp::gpio::output::${bmi088_gyro_cs}${generated_semicolon_token}\n"
        "inline constexpr bsp::gpio::input gyro_drdy = bsp::gpio::input::${bmi088_gyro_drdy}${generated_semicolon_token}\n")
    if(HAS_BMI088_HEATER)
        string(APPEND board_device_binding_body
            "inline constexpr bsp::pwm::channel heater = bsp::pwm::channel::${bmi088_heater_channel_ident}${generated_semicolon_token}\n")
    endif()
    string(APPEND board_device_binding_body "} // namespace bmi088\n\n")
endif()
if(BOARD_HAS_LED)
    string(APPEND board_device_binding_body
        "namespace led {\n"
        "inline constexpr bsp::spi::bus spi = bsp::spi::bus::${led_spi}${generated_semicolon_token}\n"
        "} // namespace led\n")
endif()
if(HAS_PS2_DEVICE)
    string(APPEND board_device_binding_body "namespace ps2 {\n")
    if(PS2_BACKEND_SPI)
        string(APPEND board_device_binding_body
            "inline constexpr bsp::spi::bus spi = bsp::spi::bus::${ps2_spi}${generated_semicolon_token}\n"
            "inline constexpr bsp::gpio::output cs = bsp::gpio::output::${ps2_cs}${generated_semicolon_token}\n")
    else()
        string(APPEND board_device_binding_body
            "inline constexpr bsp::gpio::output cmd = bsp::gpio::output::${ps2_cmd}${generated_semicolon_token}\n"
            "inline constexpr bsp::gpio::input data = bsp::gpio::input::${ps2_data}${generated_semicolon_token}\n"
            "inline constexpr bsp::gpio::output clk = bsp::gpio::output::${ps2_clk}${generated_semicolon_token}\n"
            "inline constexpr bsp::gpio::output cs = bsp::gpio::output::${ps2_cs}${generated_semicolon_token}\n")
    endif()
    string(APPEND board_device_binding_body "} // namespace ps2\n")
endif()

set(pwm_channel_config_list "")
set(pwm_channel_enum_entries "")
set(pwm_feature_macros "")
set(pwm_feature_constants "")
set(pwm_binding_cases "")
set(pwm_channel_index 0)
foreach(resource ${PNX_IOC_PWM_CHANNELS})
    if(NOT resource MATCHES "^(TIM[0-9]+)_CH([1-4])$")
        message(FATAL_ERROR "Invalid discovered PWM resource ${resource}")
    endif()
    set(timer "${CMAKE_MATCH_1}")
    set(channel_number "${CMAKE_MATCH_2}")
    string(TOLOWER "${resource}" channel_ident)
    string(TOLOWER "${timer}" timer_lower)
    pnx_ioc_timer_clock_hz("${PNX_IOC_LINES}" "${timer}" timer_clock_hz)
    list(APPEND pwm_channel_config_list "{ ${timer_clock_hz}U }")
    if(NOT pwm_channel_enum_entries STREQUAL "")
        string(APPEND pwm_channel_enum_entries ", ")
    endif()
    string(APPEND pwm_channel_enum_entries "${channel_ident} = ${pwm_channel_index}")
    string(APPEND pwm_feature_macros "#define HAS_PWM_${resource} 1\n")
    string(APPEND pwm_feature_constants
        "inline constexpr bool has_pwm_${channel_ident} = true${generated_semicolon_token}\n")
    string(APPEND pwm_binding_cases
        "    case channel::${channel_ident}: out = { &h${timer_lower}, TIM_CHANNEL_${channel_number} }${generated_semicolon_token} return true${generated_semicolon_token}\n")
    math(EXPR pwm_channel_index "${pwm_channel_index} + 1")
endforeach()
list(JOIN pwm_channel_config_list ", " pwm_config_cpp)
list(LENGTH PNX_IOC_PWM_CHANNELS pwm_channel_count)

set(pwm_app_binding_body "")
string(JSON pwm_app_binding_count ERROR_VARIABLE json_err LENGTH "${board_json}" bindings pwm_channels)
if(json_err)
    set(pwm_app_binding_count 0)
endif()
if(pwm_app_binding_count GREATER 0)
    math(EXPR pwm_app_binding_last "${pwm_app_binding_count} - 1")
    foreach(index RANGE 0 ${pwm_app_binding_last})
        string(JSON role MEMBER "${board_json}" bindings pwm_channels ${index})
        _pnx_cpp_identifier("${role}" role_ident)
        if(NOT role_ident STREQUAL role)
            message(FATAL_ERROR "PWM role '${role}' must be a C++ identifier")
        endif()
        string(JSON timer GET "${board_json}" bindings pwm_channels ${role} timer)
        string(JSON channel_number GET "${board_json}" bindings pwm_channels ${role} channel)
        string(TOUPPER "${timer}" timer)
        set(resource "${timer}_CH${channel_number}")
        list(FIND PNX_IOC_PWM_CHANNELS "${resource}" resource_index)
        if(resource_index LESS 0)
            message(FATAL_ERROR "PWM role '${role}' uses ${resource}, which is not configured for PWM in the IOC")
        endif()
        string(TOLOWER "${resource}" channel_ident)
        string(APPEND pwm_app_binding_body
            "inline constexpr bsp::pwm::channel ${role_ident} = bsp::pwm::channel::${channel_ident}${generated_semicolon_token}\n")
    endforeach()
endif()

set(adc_channel_enum_entries "")
set(adc_binding_cases "")
set(adc_channel_index 0)
foreach(entry ${PNX_IOC_ADC_BLOCKING_CHANNELS})
    if(NOT entry MATCHES "^(ADC[0-9]+)_CH([0-9]+)\\|([^|]+)\\|([^|]+)$")
        message(FATAL_ERROR "Invalid discovered ADC blocking channel ${entry}")
    endif()
    set(adc "${CMAKE_MATCH_1}")
    set(adc_channel_number "${CMAKE_MATCH_2}")
    set(adc_rank "${CMAKE_MATCH_3}")
    set(adc_sampling_time "${CMAKE_MATCH_4}")
    string(TOLOWER "${adc}_ch${adc_channel_number}" adc_channel_ident)
    string(TOLOWER "${adc}" adc_lower)
    if(NOT adc_channel_enum_entries STREQUAL "")
        string(APPEND adc_channel_enum_entries ", ")
    endif()
    string(APPEND adc_channel_enum_entries "${adc_channel_ident} = ${adc_channel_index}")
    string(APPEND adc_binding_cases
        "    case channel::${adc_channel_ident}: out = { &h${adc_lower}, ADC_CHANNEL_${adc_channel_number}, ${adc_rank}, ${adc_sampling_time} }${generated_semicolon_token} return true${generated_semicolon_token}\n")
    math(EXPR adc_channel_index "${adc_channel_index} + 1")
endforeach()
list(LENGTH PNX_IOC_ADC_BLOCKING_CHANNELS adc_channel_count)

set(adc_app_binding_body "")
string(JSON adc_app_binding_count ERROR_VARIABLE json_err LENGTH "${params_json}" bindings adc_channels)
if(json_err)
    set(adc_app_binding_count 0)
endif()
if(adc_app_binding_count GREATER 0)
    math(EXPR adc_app_binding_last "${adc_app_binding_count} - 1")
    foreach(index RANGE 0 ${adc_app_binding_last})
        string(JSON role MEMBER "${params_json}" bindings adc_channels ${index})
        _pnx_cpp_identifier("${role}" role_ident)
        if(NOT role_ident STREQUAL role)
            message(FATAL_ERROR "ADC role '${role}' must be a C++ identifier")
        endif()
        string(JSON adc GET "${params_json}" bindings adc_channels ${role} adc)
        string(JSON adc_channel_number GET "${params_json}" bindings adc_channels ${role} channel)
        string(TOUPPER "${adc}" adc)
        set(resource "${adc}_CH${adc_channel_number}")
        set(resource_found FALSE)
        foreach(entry ${PNX_IOC_ADC_BLOCKING_CHANNELS})
            if(entry MATCHES "^${resource}\\|")
                set(resource_found TRUE)
            endif()
        endforeach()
        if(NOT resource_found)
            message(FATAL_ERROR
                "ADC role '${role}' uses ${resource}, which is not an IOC single regular conversion")
        endif()
        string(TOLOWER "${resource}" adc_channel_ident)
        string(APPEND adc_app_binding_body
            "inline constexpr bsp::adc::channel ${role_ident} = bsp::adc::channel::${adc_channel_ident}${generated_semicolon_token}\n")
    endforeach()
endif()

pnx_ioc_uart_index("${PNX_IOC_UART_HW}" "${remoter_uart}" dr16_port_idx)
pnx_ioc_uart_index("${PNX_IOC_UART_HW}" "${remoter_uart}" ps2_uart_port_idx)
pnx_ioc_uart_index("${PNX_IOC_UART_HW}" "uart7" vt03_port_idx)
pnx_ioc_uart_index("${PNX_IOC_UART_HW}" "${referee_uart}" referee_port_idx)

if(dr16_port_idx GREATER_EQUAL 0)
    set(dr16_binding "${remoter_uart}")
else()
    set(dr16_binding "0")
endif()
if(vt03_port_idx GREATER_EQUAL 0)
    set(vt03_binding "uart7")
else()
    set(vt03_binding "0")
endif()
if(ps2_uart_port_idx GREATER_EQUAL 0)
    set(ps2_uart_binding "${remoter_uart}")
else()
    set(ps2_uart_binding "0")
endif()
if(referee_port_idx GREATER_EQUAL 0)
    set(referee_binding "${referee_uart}")
else()
    set(referee_binding "0")
endif()

set(active_remoter_uart "")
if(ENABLE_DR16)
    set(active_remoter_uart "${remoter_uart}")
elseif(ENABLE_VT03)
    set(active_remoter_uart "uart7")
elseif(ENABLE_PS2_UART)
    set(active_remoter_uart "${remoter_uart}")
endif()

string(JSON test_report_uart ERROR_VARIABLE json_err GET "${params_json}" test report_uart)
if(json_err)
    set(test_report_uart "uart7")
endif()
string(TOLOWER "${test_report_uart}" test_report_uart)
pnx_ioc_uart_index("${PNX_IOC_UART_HW}" "${test_report_uart}" test_report_port_idx)
if(test_report_port_idx LESS 0)
    message(FATAL_ERROR "params.test.report_uart=${test_report_uart} is not present in board/board.ioc")
endif()
if(NOT active_remoter_uart STREQUAL "" AND test_report_uart STREQUAL active_remoter_uart)
    message(FATAL_ERROR "params.test.report_uart=${test_report_uart} conflicts with the active remoter UART")
endif()
set(test_report_binding "${test_report_uart}")

# --- params namespace (explicit keys per section) ---
function(_pnx_param_float section key out_var)
    string(JSON val ERROR_VARIABLE err GET "${params_json}" ${section} ${key})
    if(err)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    if(val MATCHES "[.eE]")
        set(literal "${val}f")
    else()
        set(literal "${val}.0f")
    endif()
    set(${out_var} "  inline constexpr float ${key} = ${literal}${generated_semicolon_token}\n" PARENT_SCOPE)
endfunction()

function(_pnx_param_uint section key out_var)
    string(JSON val ERROR_VARIABLE err GET "${params_json}" ${section} ${key})
    if(err)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    set(${out_var} "  inline constexpr std::uint32_t ${key} = ${val}${generated_semicolon_token}\n" PARENT_SCOPE)
endfunction()

function(_pnx_param_bool section key out_var)
    string(JSON val ERROR_VARIABLE err GET "${params_json}" ${section} ${key})
    if(err)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    if(val STREQUAL "true" OR val STREQUAL "1" OR val STREQUAL "ON")
        set(${out_var} "  inline constexpr bool ${key} = true${generated_semicolon_token}\n" PARENT_SCOPE)
    else()
        set(${out_var} "  inline constexpr bool ${key} = false${generated_semicolon_token}\n" PARENT_SCOPE)
    endif()
endfunction()

set(params_ahrs_body "")
_pnx_param_float("ahrs" "imu_offset_x" _line)
string(APPEND params_ahrs_body "${_line}")
_pnx_param_uint("ahrs" "imu_thread_priority" _line)
string(APPEND params_ahrs_body "${_line}")
_pnx_param_uint("ahrs" "temp_thread_priority" _line)
string(APPEND params_ahrs_body "${_line}")
_pnx_param_float("ahrs" "target_temp" _line)
string(APPEND params_ahrs_body "${_line}")
if(params_ahrs_body STREQUAL "")
    string(CONCAT params_ahrs_body
        "  inline constexpr float imu_offset_x = 0.0f${generated_semicolon_token}\n"
        "  inline constexpr std::uint32_t imu_thread_priority = 3${generated_semicolon_token}\n"
        "  inline constexpr std::uint32_t temp_thread_priority = 4${generated_semicolon_token}\n"
        "  inline constexpr float target_temp = 45.0f${generated_semicolon_token}\n")
endif()

string(JSON params_dmimu_mode ERROR_VARIABLE json_err GET "${params_json}" dmimu communication_mode)
if(json_err OR params_dmimu_mode STREQUAL "")
    set(params_dmimu_mode "active")
endif()
string(TOLOWER "${params_dmimu_mode}" params_dmimu_mode_lower)
if(params_dmimu_mode_lower STREQUAL "active")
    set(params_dmimu_mode_expr "communication_mode::active")
elseif(params_dmimu_mode_lower STREQUAL "request")
    set(params_dmimu_mode_expr "communication_mode::request")
else()
    message(FATAL_ERROR "params.dmimu.communication_mode must be active or request")
endif()

string(JSON params_dmimu_offline_timeout ERROR_VARIABLE json_err GET "${params_json}" dmimu offline_timeout_ticks)
if(json_err OR params_dmimu_offline_timeout STREQUAL "")
    set(params_dmimu_offline_timeout 100)
endif()
string(JSON params_dmimu_thread_priority ERROR_VARIABLE json_err GET "${params_json}" dmimu thread_priority)
if(json_err OR params_dmimu_thread_priority STREQUAL "")
    set(params_dmimu_thread_priority 3)
endif()
string(JSON params_dmimu_receive_wait ERROR_VARIABLE json_err GET "${params_json}" dmimu receive_wait_ticks)
if(json_err OR params_dmimu_receive_wait STREQUAL "")
    set(params_dmimu_receive_wait 1)
endif()
string(JSON params_dmimu_request_period ERROR_VARIABLE json_err GET "${params_json}" dmimu request_period_ticks)
if(json_err OR params_dmimu_request_period STREQUAL "")
    set(params_dmimu_request_period 1)
endif()
if(params_dmimu_offline_timeout LESS 1 OR params_dmimu_receive_wait LESS 1)
    message(FATAL_ERROR "params.dmimu offline_timeout_ticks and receive_wait_ticks must be greater than zero")
endif()
if(params_dmimu_mode_lower STREQUAL "request" AND params_dmimu_request_period LESS 1)
    message(FATAL_ERROR "params.dmimu.request_period_ticks must be greater than zero in request mode")
endif()
string(CONCAT params_dmimu_body
    "enum class communication_mode : std::uint8_t { request = 0, active }${generated_semicolon_token}\n"
    "inline constexpr communication_mode mode = ${params_dmimu_mode_expr}${generated_semicolon_token}\n"
    "inline constexpr std::uint32_t offline_timeout_ticks = ${params_dmimu_offline_timeout}U${generated_semicolon_token}\n"
    "inline constexpr std::uint32_t thread_priority = ${params_dmimu_thread_priority}U${generated_semicolon_token}\n"
    "inline constexpr std::uint32_t receive_wait_ticks = ${params_dmimu_receive_wait}U${generated_semicolon_token}\n"
    "inline constexpr std::uint32_t request_period_ticks = ${params_dmimu_request_period}U${generated_semicolon_token}\n")

set(params_remoter_body "")
_pnx_param_uint("remoter" "thread_priority" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr std::uint32_t thread_priority = 2${generated_semicolon_token}\n")
endif()
string(APPEND params_remoter_body "${_line}")
_pnx_param_uint("remoter" "rx_timeout_ticks" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr std::uint32_t rx_timeout_ticks = 100${generated_semicolon_token}\n")
endif()
string(APPEND params_remoter_body "${_line}")
_pnx_param_uint("remoter" "offline_timeout_ticks" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr std::uint32_t offline_timeout_ticks = 120${generated_semicolon_token}\n")
endif()
string(APPEND params_remoter_body "${_line}")
_pnx_param_uint("remoter" "ps2_uart_offline_timeout_ticks" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr std::uint32_t ps2_uart_offline_timeout_ticks = 600${generated_semicolon_token}\n")
endif()
string(APPEND params_remoter_body "${_line}")
_pnx_param_uint("remoter" "ps2_uart_frame_timeout_ticks" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr std::uint32_t ps2_uart_frame_timeout_ticks = 20${generated_semicolon_token}\n")
endif()
string(APPEND params_remoter_body "${_line}")
_pnx_param_float("remoter" "ps2_uart_deadzone" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr float ps2_uart_deadzone = 0.08f${generated_semicolon_token}\n")
endif()
string(APPEND params_remoter_body "${_line}")

set(params_referee_body "")
_pnx_param_uint("referee" "thread_priority" _line)
string(APPEND params_referee_body "${_line}")
if(params_referee_body STREQUAL "")
    set(params_referee_body "  inline constexpr std::uint32_t thread_priority = 8${generated_semicolon_token}\n")
endif()

set(params_test_body "")
_pnx_param_uint("test" "thread_priority" _line)
string(APPEND params_test_body "${_line}")
_pnx_param_bool("test" "auto_run_on_boot" _line)
string(APPEND params_test_body "${_line}")
if(params_test_body STREQUAL "")
    string(CONCAT params_test_body
        "  inline constexpr std::uint32_t thread_priority = 10${generated_semicolon_token}\n"
        "  inline constexpr bool auto_run_on_boot = true${generated_semicolon_token}\n")
endif()

set(params_usb_body "")
_pnx_param_uint("usb" "read_thread_priority" _line)
string(APPEND params_usb_body "${_line}")
_pnx_param_uint("usb" "write_thread_priority" _line)
string(APPEND params_usb_body "${_line}")
_pnx_param_uint("usb" "period_ticks" _line)
string(APPEND params_usb_body "${_line}")
if(params_usb_body STREQUAL "")
    string(CONCAT params_usb_body
        "  inline constexpr std::uint32_t read_thread_priority = 5${generated_semicolon_token}\n"
        "  inline constexpr std::uint32_t write_thread_priority = 5${generated_semicolon_token}\n"
        "  inline constexpr std::uint32_t period_ticks = 2${generated_semicolon_token}\n")
endif()

set(params_can_diag_body "")
_pnx_param_uint("can_diag" "sample_period_ms" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr std::uint32_t sample_period_ms = 1000${generated_semicolon_token}\n")
endif()
string(APPEND params_can_diag_body "${_line}")

_pnx_param_uint("can_diag" "window_size" _line)
if(_line STREQUAL "")
    set(_line "  inline constexpr std::uint32_t window_size = 60${generated_semicolon_token}\n")
endif()
string(APPEND params_can_diag_body "${_line}")

if(MOTOR_DJI)
    set(MOTOR_DJI_C 1)
else()
    set(MOTOR_DJI_C 0)
endif()
if(MOTOR_DM)
    set(MOTOR_DM_C 1)
else()
    set(MOTOR_DM_C 0)
endif()
if(MOTOR_LK)
    set(MOTOR_LK_C 1)
else()
    set(MOTOR_LK_C 0)
endif()
if(MOTOR_XV2)
    set(MOTOR_XV2_C 1)
else()
    set(MOTOR_XV2_C 0)
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")

set(CONFIG_HPP "${OUT_DIR}/config.hpp")
set(ROBOT_CONFIG_HPP "${OUT_DIR}/robot_config.hpp")
set(BSP_BINDINGS_CPP "${OUT_DIR}/bsp_bindings.cpp")
set(BSP_BINDINGS_HPP "${OUT_DIR}/bsp_bindings.hpp")

file(WRITE "${CONFIG_HPP}"
"#pragma once\n"
"// Generated from board/board.ioc + configs/params.json + configs/robot.json. Do not edit.\n\n"
"#include <array>\n"
"#include <cstddef>\n"
"#include <cstdint>\n\n"
"#define HW_HAS_USB ${HW_HAS_USB}\n"
"#define ENABLE_USBX ${ENABLE_USBX_C}\n"
"#define HAS_AHRS ${HAS_AHRS}\n"
"#define HAS_BMI088_HEATER ${HAS_BMI088_HEATER}\n"
"#define HAS_DMIMU ${HAS_DMIMU}\n"
"#define HAS_REMOTER ${HAS_REMOTER}\n"
"#define HAS_VT03 ${HAS_VT03}\n"
"#define HAS_PS2_DEVICE ${HAS_PS2_DEVICE}\n"
"#define PS2_BACKEND_SPI ${PS2_BACKEND_SPI}\n"
"#define PS2_BACKEND_GPIO ${PS2_BACKEND_GPIO}\n"
"#define HAS_PS2_UART ${HAS_PS2_UART}\n"
"#define ENABLE_DR16 ${ENABLE_DR16}\n"
"#define ENABLE_VT03 ${ENABLE_VT03}\n"
"#define ENABLE_PS2 ${ENABLE_PS2}\n"
"#define ENABLE_PS2_UART ${ENABLE_PS2_UART}\n"
"#define HAS_REFEREE ${HAS_REFEREE}\n"
"#define HAS_UI ${HAS_UI}\n"
"#define HAS_LED ${HAS_LED}\n"
"${pwm_feature_macros}"
"#define HAS_MOTORS ${HAS_MOTORS}\n"
"#define CAN_DIAG_ENABLED ${CAN_DIAG_ENABLED}\n"
"#define MOTOR_DJI ${MOTOR_DJI_C}\n"
"#define MOTOR_DM ${MOTOR_DM_C}\n"
"#define MOTOR_LK ${MOTOR_LK_C}\n"
"#define MOTOR_XV2 ${MOTOR_XV2_C}\n\n"
"namespace config::feature {\n\n"
"inline constexpr bool hw_has_usb = ${HW_HAS_USB};\n"
"inline constexpr bool enable_usbx = ${ENABLE_USBX_C};\n"
"inline constexpr bool has_ahrs = ${HAS_AHRS};\n"
"inline constexpr bool has_bmi088_heater = ${HAS_BMI088_HEATER};\n"
"inline constexpr bool has_dmimu = ${HAS_DMIMU};\n"
"inline constexpr bool has_remoter = ${HAS_REMOTER};\n"
"inline constexpr bool has_vt03 = ${HAS_VT03};\n"
"inline constexpr bool has_ps2_device = ${HAS_PS2_DEVICE};\n"
"inline constexpr bool has_ps2_uart = ${HAS_PS2_UART};\n"
"inline constexpr bool enable_dr16 = ${ENABLE_DR16};\n"
"inline constexpr bool enable_vt03 = ${ENABLE_VT03};\n"
"inline constexpr bool enable_ps2 = ${ENABLE_PS2};\n"
"inline constexpr bool enable_ps2_uart = ${ENABLE_PS2_UART};\n"
"inline constexpr bool has_referee = ${HAS_REFEREE};\n"
"inline constexpr bool has_ui = ${HAS_UI};\n"
"inline constexpr bool has_led = ${HAS_LED};\n"
"${pwm_feature_constants}"
"inline constexpr bool has_motors = ${HAS_MOTORS};\n"
"inline constexpr bool motor_dji = ${MOTOR_DJI_C};\n"
"inline constexpr bool motor_dm = ${MOTOR_DM_C};\n"
"inline constexpr bool motor_lk = ${MOTOR_LK_C};\n"
"inline constexpr bool motor_xv2 = ${MOTOR_XV2_C};\n\n"
"inline constexpr bool can_diag = ${CAN_DIAG_ENABLED};\n\n"
"} // namespace config::feature\n\n"
"namespace bsp {\n"
"namespace can {\n\n"
"enum class bus_type : std::uint8_t { classic = 0, fd = 1 };\n"
"enum class bus_capability : std::uint8_t { classic = 0, fd_no_brs = 1, fd_brs = 2 };\n"
"enum class id_type : std::uint8_t { standard = 0, extended = 1 };\n"
"enum class handle_id : std::uint8_t { none = 0, fdcan1, fdcan2, fdcan3 };\n"
"enum class bus : std::uint8_t { ${can_bus_enum_entries} };\n\n"
"struct bus_config\n"
"{\n"
"    bool enabled = false;\n"
"    handle_id handle = handle_id::none;\n"
"    bus_type type = bus_type::classic;\n"
"    bus_capability capability = bus_capability::classic;\n"
"    id_type filter_id_type = id_type::standard;\n"
"};\n\n"
"inline constexpr std::size_t bus_count = ${can_bus_count};\n"
"inline constexpr std::size_t max_rx_callbacks = ${can_max_rx_callbacks};\n"
"inline constexpr std::array<bus_config, bus_count> configs = {{ ${can_config_cpp} }};\n"
"inline constexpr std::array<bool, bus_count> enabled = { ${can_enabled_cpp} };\n"
"inline constexpr std::array<bus_type, bus_count> configured_bus_types = { ${can_type_cpp} };\n"
"inline constexpr std::array<id_type, bus_count> filter_id_types = { ${can_id_type_cpp} };\n\n"
"} // namespace can\n\n"
"namespace spi {\n\n"
"enum class handle_id : std::uint8_t { ${spi_handle_enum_entries} };\n"
"enum class bus : std::uint8_t { ${spi_bus_enum_entries} };\n\n"
"struct bus_config\n"
"{\n"
"    bool enabled = false;\n"
"    handle_id handle = handle_id::none;\n"
"};\n\n"
"inline constexpr std::size_t bus_count = ${spi_bus_count};\n"
"inline constexpr std::array<bus_config, bus_count> configs = {{ ${spi_config_cpp} }};\n\n"
"} // namespace spi\n\n"
"namespace gpio {\n\n"
"enum class port_id : std::uint8_t { none = 0, a, b, c, d, e, f, g, h, i, j, k }${generated_semicolon_token}\n"
"enum class active_level : std::uint8_t { low = 0, high = 1 }${generated_semicolon_token}\n"
"enum class input : std::uint8_t { ${gpio_input_enum_entries} }${generated_semicolon_token}\n"
"enum class output : std::uint8_t { ${gpio_output_enum_entries} }${generated_semicolon_token}\n\n"
"struct input_config { port_id port${generated_semicolon_token} std::uint8_t pin${generated_semicolon_token} active_level active${generated_semicolon_token} }${generated_semicolon_token}\n"
"struct output_config { port_id port${generated_semicolon_token} std::uint8_t pin${generated_semicolon_token} active_level active${generated_semicolon_token} }${generated_semicolon_token}\n\n"
"inline constexpr std::size_t input_count = ${gpio_input_count}${generated_semicolon_token}\n"
"inline constexpr std::size_t output_count = ${gpio_output_count}${generated_semicolon_token}\n"
"inline constexpr std::array<input_config, input_count> input_configs = {{ ${gpio_input_config_cpp} }}${generated_semicolon_token}\n"
"inline constexpr std::array<output_config, output_count> output_configs = {{ ${gpio_output_config_cpp} }}${generated_semicolon_token}\n\n"
"} // namespace gpio\n\n"
"namespace pwm {\n\n"
"enum class channel : std::uint8_t { ${pwm_channel_enum_entries} }${generated_semicolon_token}\n\n"
"struct channel_config\n"
"{\n"
"    std::uint32_t timer_clock_hz = 0;\n"
"};\n\n"
"inline constexpr std::size_t channel_count = ${pwm_channel_count};\n"
"inline constexpr std::array<channel_config, channel_count> configs = {{ ${pwm_config_cpp} }};\n\n"
"} // namespace pwm\n\n"
"namespace adc {\n\n"
"enum class channel : std::uint8_t { ${adc_channel_enum_entries} }${generated_semicolon_token}\n"
"inline constexpr std::size_t channel_count = ${adc_channel_count}${generated_semicolon_token}\n\n"
"} // namespace adc\n\n"
"namespace usart {\n\n"
"using port = std::size_t;\n\n"
"enum class handle_id : std::uint8_t { none = 0, uart5, uart7, usart1, usart10 };\n\n"
"struct port_config\n"
"{\n"
"    bool enabled = false;\n"
"    handle_id handle = handle_id::none;\n"
"    bool has_rx_dma = false;\n"
"    bool has_tx_dma = false;\n"
"};\n\n"
"inline constexpr std::size_t port_count = ${usart_port_count};\n"
"inline constexpr std::array<port_config, port_count> configs = {{ ${usart_config_cpp} }};\n"
"inline constexpr std::array<bool, port_count> enabled = { ${usart_enabled_cpp} };\n\n"
"} // namespace usart\n"
"} // namespace bsp\n\n"
"namespace app {\n"
"namespace can {\n\n"
"${can_app_binding_body}"
"} // namespace can\n"
"namespace spi {\n\n"
"${spi_app_binding_body}"
"} // namespace spi\n"
"namespace uart {\n\n"
"${uart_binding_body}\n\n"
"${uart_app_binding_body}"
"inline constexpr bsp::usart::port dr16 = ${dr16_binding};\n"
"inline constexpr bsp::usart::port vt03 = ${vt03_binding};\n"
"inline constexpr bsp::usart::port ps2_uart = ${ps2_uart_binding};\n"
"inline constexpr bsp::usart::port referee = ${referee_binding};\n"
"inline constexpr bsp::usart::port test_report = ${test_report_binding};\n\n"
"} // namespace uart\n"
"\nnamespace gpio {\n\n"
"${gpio_input_binding_body}${gpio_output_binding_body}"
"\n} // namespace gpio\n"
"\nnamespace pwm {\n\n"
"${pwm_app_binding_body}"
"\n} // namespace pwm\n"
"\nnamespace adc {\n\n"
"${adc_app_binding_body}"
"\n} // namespace adc\n"
"} // namespace app\n\n"
"namespace board::memory {\n\n"
"struct address_range { std::uintptr_t start${generated_semicolon_token} std::uintptr_t end${generated_semicolon_token} }${generated_semicolon_token}\n"
"inline constexpr bool dma_dedicated_section = ${PNX_DMA_DEDICATED_SECTION}${generated_semicolon_token}\n"
"inline constexpr std::size_t cache_line_size = ${PNX_DMA_CACHE_LINE_SIZE}U${generated_semicolon_token}\n"
"inline constexpr std::array<address_range, ${dma_accessible_range_count}> dma_accessible_ranges = {{ ${dma_accessible_ranges_cpp} }}${generated_semicolon_token}\n\n"
"} // namespace board::memory\n\n"
"namespace board::device {\n\n"
"${board_device_binding_body}"
"} // namespace board::device\n\n"
"namespace params::ahrs {\n"
"${params_ahrs_body}"
"} // namespace params::ahrs\n\n"
"namespace params::dmimu {\n"
"${params_dmimu_body}"
"} // namespace params::dmimu\n\n"
"namespace params::remoter {\n"
"${params_remoter_body}"
"} // namespace params::remoter\n\n"
"namespace params::referee {\n"
"${params_referee_body}"
"} // namespace params::referee\n\n"
"namespace params::test {\n"
"${params_test_body}"
"} // namespace params::test\n\n"
"namespace params::usb {\n"
"${params_usb_body}"
"} // namespace params::usb\n"
"namespace params::can_diag {\n"
"${params_can_diag_body}"
"} // namespace params::can_diag\n"
)
file(READ "${CONFIG_HPP}" config_hpp_raw)
string(REPLACE "${generated_semicolon_token}" ";" config_hpp_fixed "${config_hpp_raw}")
file(WRITE "${CONFIG_HPP}" "${config_hpp_fixed}")

message(STATUS "Generated ${CONFIG_HPP}")

set(ps2_device_binding_body "")
if(HAS_PS2_DEVICE)
    if(PS2_BACKEND_SPI)
        string(APPEND ps2_device_binding_body
            "using ps2_transport = ps2_spi${generated_semicolon_token}\n\n"
            "inline ps2& ps2_instance()\n"
            "{\n"
            "    static ps2_transport transport{board::device::ps2::spi, board::device::ps2::cs}${generated_semicolon_token}\n"
            "    static ps2 controller{transport}${generated_semicolon_token}\n"
            "    return controller${generated_semicolon_token}\n"
            "}\n")
    else()
        string(APPEND ps2_device_binding_body
            "using ps2_transport = ps2_gpio${generated_semicolon_token}\n\n"
            "inline ps2& ps2_instance()\n"
            "{\n"
            "    static ps2_transport transport{board::device::ps2::cmd, board::device::ps2::data,\n"
            "                                   board::device::ps2::clk, board::device::ps2::cs}${generated_semicolon_token}\n"
            "    static ps2 controller{transport}${generated_semicolon_token}\n"
            "    return controller${generated_semicolon_token}\n"
            "}\n")
    endif()
endif()

file(WRITE "${BSP_BINDINGS_HPP}"
"#pragma once\n"
"// Generated from board/board.ioc and configs/params.json. Do not edit.\n\n"
"#include \"config.hpp\"\n\n"
"#if HAS_PS2_DEVICE\n"
"#include \"ps2.hpp\"\n\n"
"namespace remoter::binding {\n\n"
"${ps2_device_binding_body}"
"} // namespace remoter::binding\n"
"#endif // HAS_PS2_DEVICE\n")
file(READ "${BSP_BINDINGS_HPP}" bsp_bindings_hpp_raw)
string(REPLACE "${generated_semicolon_token}" ";" bsp_bindings_hpp_fixed "${bsp_bindings_hpp_raw}")
file(WRITE "${BSP_BINDINGS_HPP}" "${bsp_bindings_hpp_fixed}")
message(STATUS "Generated ${BSP_BINDINGS_HPP}")

file(WRITE "${BSP_BINDINGS_CPP}"
"// Generated from board/board.ioc. Do not edit.\n\n"
"#include \"bsp_bindings.hpp\"\n"
"#include \"bsp_adc.hpp\"\n"
"#include \"bsp_pwm.hpp\"\n"
"#include \"bsp_spi.hpp\"\n"
"#include \"adc.h\"\n"
"#include \"spi.h\"\n"
"#include \"tim.h\"\n\n"
"namespace bsp::pwm::detail {\n\n"
"bool binding_for(channel channel_id, binding& out) noexcept\n"
"{\n"
"    switch (channel_id)\n"
"    {\n"
"${pwm_binding_cases}"
"    default: return false${generated_semicolon_token}\n"
"    }\n"
"}\n\n"
"} // namespace bsp::pwm::detail\n\n"
"namespace bsp::adc::detail {\n\n"
"bool binding_for(channel channel_id, binding& out) noexcept\n"
"{\n"
"    switch (channel_id)\n"
"    {\n"
"${adc_binding_cases}"
"    default: return false${generated_semicolon_token}\n"
"    }\n"
"}\n\n"
"} // namespace bsp::adc::detail\n\n"
"namespace bsp::spi::detail {\n\n"
"bool binding_for(bus bus_id, binding& out) noexcept\n"
"{\n"
"    switch (bus_id)\n"
"    {\n"
"${spi_binding_cases}"
"    default: return false${generated_semicolon_token}\n"
"    }\n"
"}\n\n"
"} // namespace bsp::spi::detail\n")
file(READ "${BSP_BINDINGS_CPP}" bsp_bindings_raw)
string(REPLACE "${generated_semicolon_token}" ";" bsp_bindings_fixed "${bsp_bindings_raw}")
file(WRITE "${BSP_BINDINGS_CPP}" "${bsp_bindings_fixed}")
message(STATUS "Generated ${BSP_BINDINGS_CPP}")

function(_pnx_motor_type_flag model out_var)
    string(TOLOWER "${model}" model_lower)
    if(model_lower MATCHES "^dji_")
        set(${out_var} "Dji" PARENT_SCOPE)
    elseif(model_lower MATCHES "^dm_")
        set(${out_var} "Dm" PARENT_SCOPE)
    elseif(model_lower MATCHES "^lk_")
        set(${out_var} "Lk" PARENT_SCOPE)
    elseif(model_lower MATCHES "^xv2_")
        set(${out_var} "Xv2" PARENT_SCOPE)
    else()
        set(${out_var} "Other" PARENT_SCOPE)
    endif()
endfunction()

function(_pnx_motor_control_mode_expr mode out_var)
    string(TOLOWER "${mode}" mode_lower)
    if(mode_lower STREQUAL "" OR mode_lower STREQUAL "relax")
        set(${out_var} "::motors::mode::relax" PARENT_SCOPE)
    elseif(mode_lower STREQUAL "current")
        set(${out_var} "::motors::mode::current" PARENT_SCOPE)
    elseif(mode_lower STREQUAL "torque")
        set(${out_var} "::motors::mode::torque" PARENT_SCOPE)
    elseif(mode_lower STREQUAL "mit")
        set(${out_var} "::motors::mode::mit" PARENT_SCOPE)
    elseif(mode_lower STREQUAL "pos_speed" OR mode_lower STREQUAL "position_speed")
        set(${out_var} "::motors::mode::pos_speed" PARENT_SCOPE)
    elseif(mode_lower STREQUAL "speed" OR mode_lower STREQUAL "velocity")
        set(${out_var} "::motors::mode::speed" PARENT_SCOPE)
    elseif(mode_lower STREQUAL "multi")
        set(${out_var} "::motors::mode::multi" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "robot motor control_mode must be relax, current, torque, mit, pos_speed, speed, or multi")
    endif()
endfunction()

function(_pnx_motor_model_expr model out_var)
    string(TOLOWER "${model}" model_lower)
    if(model_lower STREQUAL "dji_m2006")
        set(${out_var} "model::dji_m2006" PARENT_SCOPE)
    elseif(model_lower STREQUAL "dji_m3508")
        set(${out_var} "model::dji_m3508" PARENT_SCOPE)
    elseif(model_lower STREQUAL "dji_gm6020")
        set(${out_var} "model::dji_gm6020" PARENT_SCOPE)
    elseif(model_lower STREQUAL "dji_xroll")
        set(${out_var} "model::dji_xroll" PARENT_SCOPE)
    elseif(model_lower STREQUAL "dm_dm4310")
        set(${out_var} "model::dm_dm4310" PARENT_SCOPE)
    elseif(model_lower STREQUAL "dm_dm8009p")
        set(${out_var} "model::dm_dm8009p" PARENT_SCOPE)
    elseif(model_lower STREQUAL "lk_lk8016")
        set(${out_var} "model::lk_lk8016" PARENT_SCOPE)
    elseif(model_lower STREQUAL "lk_lk9025")
        set(${out_var} "model::lk_lk9025" PARENT_SCOPE)
    elseif(model_lower STREQUAL "unknown" OR model_lower STREQUAL "")
        set(${out_var} "model::unknown" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "robot motor model ${model} is not supported")
    endif()
endfunction()

set(robot_motor_configs_body "")
set(robot_motor_count 0)
set(robot_has_dji 0)
set(robot_has_dm 0)
set(robot_has_lk 0)
set(robot_has_xv2 0)
set(robot_has_other 0)
set(robot_dm_id_base "0x01")
set(robot_dm_master_id_base "0x05")
set(robot_dm_max_motors "4")
set(robot_dmimu_include "")
set(robot_dmimu_body "// DMIMU is not enabled in the robot device tree.\n")

if(DEFINED ROBOT_CONFIG AND EXISTS "${ROBOT_CONFIG}")
    if(HAS_DMIMU)
        string(JSON robot_dmimu_can_bus ERROR_VARIABLE json_err GET "${robot_json}" devices dmimu can_bus)
        if(json_err OR robot_dmimu_can_bus STREQUAL "")
            message(FATAL_ERROR "enabled robot devices.dmimu requires can_bus")
        endif()
        string(JSON robot_dmimu_can_type ERROR_VARIABLE json_err GET "${robot_json}" devices dmimu can_type)
        if(json_err OR robot_dmimu_can_type STREQUAL "")
            message(FATAL_ERROR "enabled robot devices.dmimu requires can_type=classic")
        endif()
        string(JSON robot_dmimu_can_id ERROR_VARIABLE json_err GET "${robot_json}" devices dmimu can_id)
        if(json_err OR robot_dmimu_can_id STREQUAL "")
            message(FATAL_ERROR "enabled robot devices.dmimu requires can_id")
        endif()
        string(JSON robot_dmimu_master_id ERROR_VARIABLE json_err GET "${robot_json}" devices dmimu master_id)
        if(json_err OR robot_dmimu_master_id STREQUAL "")
            message(FATAL_ERROR "enabled robot devices.dmimu requires master_id")
        endif()

        string(TOLOWER "${robot_dmimu_can_bus}" robot_dmimu_can_bus_lower)
        string(TOLOWER "${robot_dmimu_can_type}" robot_dmimu_can_type_lower)
        pnx_ioc_hw_in_list("${PNX_IOC_FDCAN_HW}" "${robot_dmimu_can_bus_lower}" robot_dmimu_can_bus_present)
        if(NOT robot_dmimu_can_bus_present)
            message(FATAL_ERROR "robot DMIMU uses ${robot_dmimu_can_bus_lower}, but it is not present in ${IOC}")
        endif()
        if(NOT robot_dmimu_can_type_lower STREQUAL "classic")
            message(FATAL_ERROR "robot DMIMU only supports can_type=classic")
        endif()
        set(robot_dmimu_bus_type_var "PNX_CAN_TYPE_${robot_dmimu_can_bus_lower}")
        if(NOT "${${robot_dmimu_bus_type_var}}" STREQUAL "classic")
            message(FATAL_ERROR
                "robot DMIMU requests classic CAN, but board ${robot_dmimu_can_bus_lower} is configured as ${${robot_dmimu_bus_type_var}}")
        endif()

        math(EXPR robot_dmimu_can_id_value "${robot_dmimu_can_id}")
        math(EXPR robot_dmimu_master_id_value "${robot_dmimu_master_id}")
        if(robot_dmimu_can_id_value LESS 0 OR robot_dmimu_can_id_value GREATER 255)
            message(FATAL_ERROR "robot DMIMU can_id must be in the uint8 range 0x00..0xFF")
        endif()
        if(robot_dmimu_master_id_value LESS 0 OR robot_dmimu_master_id_value GREATER 255)
            message(FATAL_ERROR "robot DMIMU master_id must be in the uint8 range 0x00..0xFF")
        endif()

        set(robot_dmimu_include "#include \"dmimu.hpp\"\n")
        string(CONCAT robot_dmimu_body
            "inline constexpr ::imu::dmimu::transport_config dmimu{\n"
            "        bsp::can::bus::${robot_dmimu_can_bus_lower},\n"
            "        bsp::can::bus_type::classic,\n"
            "        ${robot_dmimu_can_id}U,\n"
            "        ${robot_dmimu_master_id}U,\n"
            "};\n")
    endif()

    string(JSON robot_dm_id_base_json ERROR_VARIABLE json_err GET "${robot_json}" devices motors dm id_base)
    if(NOT json_err AND NOT robot_dm_id_base_json STREQUAL "")
        set(robot_dm_id_base "${robot_dm_id_base_json}")
    endif()
    string(JSON robot_dm_master_id_base_json ERROR_VARIABLE json_err GET "${robot_json}" devices motors dm master_id_base)
    if(NOT json_err AND NOT robot_dm_master_id_base_json STREQUAL "")
        set(robot_dm_master_id_base "${robot_dm_master_id_base_json}")
    endif()
    string(JSON robot_dm_max_motors_json ERROR_VARIABLE json_err GET "${robot_json}" devices motors dm max_motors)
    if(NOT json_err AND NOT robot_dm_max_motors_json STREQUAL "")
        set(robot_dm_max_motors "${robot_dm_max_motors_json}")
    endif()

    string(JSON motor_count ERROR_VARIABLE json_err LENGTH "${robot_json}" devices motors list)
    if(json_err)
        set(motor_count 0)
    endif()

    if(motor_count GREATER 0)
        math(EXPR motor_last_index "${motor_count} - 1")
        foreach(i RANGE 0 ${motor_last_index})
            string(JSON motor_name ERROR_VARIABLE json_err GET "${robot_json}" devices motors list ${i} name)
            if(json_err OR motor_name STREQUAL "")
                message(FATAL_ERROR "robot motor at index ${i} requires a non-empty name")
            endif()
            string(JSON motor_model ERROR_VARIABLE json_err GET "${robot_json}" devices motors list ${i} model)
            if(json_err OR motor_model STREQUAL "")
                set(motor_model "unknown")
            endif()
            string(JSON motor_can_bus ERROR_VARIABLE json_err GET "${robot_json}" devices motors list ${i} can_bus)
            if(json_err OR motor_can_bus STREQUAL "")
                message(FATAL_ERROR "robot motor ${motor_name} requires can_bus")
            endif()
            string(JSON motor_can_type ERROR_VARIABLE json_err GET "${robot_json}" devices motors list ${i} can_type)
            if(json_err OR motor_can_type STREQUAL "")
                message(FATAL_ERROR "robot motor ${motor_name} requires can_type")
            endif()
            string(JSON motor_can_id ERROR_VARIABLE json_err GET "${robot_json}" devices motors list ${i} can_id)
            if(json_err OR motor_can_id STREQUAL "")
                message(FATAL_ERROR "robot motor ${motor_name} requires can_id")
            endif()
            string(JSON motor_control_mode ERROR_VARIABLE json_err GET "${robot_json}" devices motors list ${i} control_mode)
            if(json_err)
                set(motor_control_mode "relax")
            endif()

            string(TOLOWER "${motor_can_bus}" motor_can_bus_lower)
            string(TOLOWER "${motor_can_type}" motor_can_type_lower)
            pnx_ioc_hw_in_list("${PNX_IOC_FDCAN_HW}" "${motor_can_bus_lower}" motor_can_bus_present)
            if(NOT motor_can_bus_present)
                message(FATAL_ERROR "robot motor ${motor_name} uses ${motor_can_bus_lower}, but it is not present in ${IOC}")
            endif()
            if(NOT motor_can_type_lower STREQUAL "classic" AND NOT motor_can_type_lower STREQUAL "fd")
                message(FATAL_ERROR "robot motor ${motor_name} can_type must be classic or fd")
            endif()

            set(motor_bus_type_var "PNX_CAN_TYPE_${motor_can_bus_lower}")
            if(NOT motor_can_type_lower STREQUAL "${${motor_bus_type_var}}")
                message(FATAL_ERROR
                    "robot motor ${motor_name} requests ${motor_can_type_lower}, but board ${motor_can_bus_lower} is configured as ${${motor_bus_type_var}}")
            endif()

            math(EXPR motor_can_id_value "${motor_can_id}")
            set(motor_bus_id_type_var "PNX_CAN_ID_TYPE_${motor_can_bus_lower}")
            if("${${motor_bus_id_type_var}}" STREQUAL "standard")
                set(motor_can_id_max 2047)
            else()
                set(motor_can_id_max 536870911)
            endif()
            if(motor_can_id_value LESS 0 OR motor_can_id_value GREATER motor_can_id_max)
                message(FATAL_ERROR
                    "robot motor ${motor_name} CAN ID ${motor_can_id} is out of range for ${${motor_bus_id_type_var}} frames")
            endif()

            _pnx_cpp_identifier("${motor_name}" motor_ident)
            string(REGEX MATCH "^[A-Za-z_][A-Za-z0-9_]*$" valid_ident "${motor_ident}")
            if(NOT valid_ident)
                message(FATAL_ERROR "robot motor ${motor_name} cannot be converted to a valid C++ identifier")
            endif()

            _pnx_motor_type_flag("${motor_model}" motor_type_flag)
            _pnx_motor_control_mode_expr("${motor_control_mode}" motor_control_mode_expr)
            _pnx_motor_model_expr("${motor_model}" motor_model_expr)
            if(motor_type_flag STREQUAL "Dji")
                set(robot_has_dji 1)
            elseif(motor_type_flag STREQUAL "Dm")
                set(robot_has_dm 1)
            elseif(motor_type_flag STREQUAL "Lk")
                set(robot_has_lk 1)
            elseif(motor_type_flag STREQUAL "Xv2")
                set(robot_has_xv2 1)
            else()
                set(robot_has_other 1)
            endif()

            string(APPEND robot_motor_configs_body
                "// ${motor_model}\n"
                "inline constexpr model ${motor_ident}_model = ${motor_model_expr};\n"
                "inline constexpr ::motors::config ${motor_ident}{\n"
                "    bsp::can::bus::${motor_can_bus_lower},\n"
                "    bsp::can::bus_type::${motor_can_type_lower},\n"
                "    ${motor_can_id}U,\n"
                "    ${motor_control_mode_expr},\n"
                "};\n\n")
            math(EXPR robot_motor_count "${robot_motor_count} + 1")
        endforeach()
    endif()
endif()

if(robot_motor_configs_body STREQUAL "")
    set(robot_motor_configs_body "// No motors are described in the robot device tree.\n")
endif()

file(WRITE "${ROBOT_CONFIG_HPP}"
"#pragma once\n"
"// Generated from robot device tree. Do not edit.\n\n"
"#include \"config.hpp\"\n"
"#include \"motor.hpp\"\n\n"
"${robot_dmimu_include}\n"
"#include <cstddef>\n"
"#include <cstdint>\n\n"
"namespace robot::motors {\n\n"
"inline constexpr std::size_t motor_count = ${robot_motor_count};\n"
"inline constexpr bool has_dji = ${robot_has_dji};\n"
"inline constexpr bool has_dm = ${robot_has_dm};\n"
"inline constexpr bool has_lk = ${robot_has_lk};\n"
"inline constexpr bool has_xv2 = ${robot_has_xv2};\n"
"inline constexpr bool has_other = ${robot_has_other};\n\n"
"enum class model : std::uint8_t {\n"
"    unknown = 0,\n"
"    dji_m2006,\n"
"    dji_m3508,\n"
"    dji_gm6020,\n"
"    dji_xroll,\n"
"    dm_dm4310,\n"
"    dm_dm8009p,\n"
"    lk_lk8016,\n"
"    lk_lk9025,\n"
"};\n\n"
"namespace dm {\n"
"inline constexpr std::uint32_t id_base = ${robot_dm_id_base}U;\n"
"inline constexpr std::uint32_t master_id_base = ${robot_dm_master_id_base}U;\n"
"inline constexpr std::size_t max_motors = ${robot_dm_max_motors};\n"
"} // namespace dm\n\n"
"${robot_motor_configs_body}"
"} // namespace robot::motors\n\n"
"namespace robot::imu {\n\n"
"inline constexpr bool has_dmimu = ${HAS_DMIMU};\n"
"${robot_dmimu_body}"
"\n} // namespace robot::imu\n")

message(STATUS "Generated ${ROBOT_CONFIG_HPP}")
