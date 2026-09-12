#pragma once

#include "config.hpp"
#include "demo_protocol.hpp"

#include <cstdint>

namespace diagnose::debug
{

struct link_state
{
    bool started = false;
    bool ready = false;
    bool connected = false;
    std::uint8_t last_status = protocol::status_code(types::status::not_configured);
    std::uint32_t rx_count = 0;
    std::uint32_t tx_count = 0;
    std::uint32_t error_count = 0;
    std::uint32_t last_seq = 0;
    std::uint32_t last_counter = 0;
    float last_value = 0.0f;
    bool last_flag = false;
    bool bsp_read_busy = false;
    bool bsp_write_busy = false;
    std::uint32_t bsp_last_read_status = 0;
    std::uint32_t bsp_last_write_status = 0;
    std::uint16_t bsp_last_read_len = 0;
    std::uint16_t bsp_last_write_requested = 0;
    std::uint16_t bsp_last_write_actual = 0;
    std::uint16_t bsp_pending_write_len = 0;
    bool bsp_pending_write = false;
    bool bsp_in_flight_write = false;
    std::uint32_t bsp_read_count = 0;
    std::uint32_t bsp_write_count = 0;
    std::uint32_t bsp_error_count = 0;
    std::uint32_t bsp_tx_wake_count = 0;
    bool tx_pending = false;
    std::uint32_t tx_pending_seq = 0;
    protocol::host_packet last_rx{};
    protocol::device_packet last_tx{};
};

struct unit_test_state
{
    bool started = false;
    bool passed = false;
    std::uint32_t total_count = 0;
    std::uint32_t passed_count = 0;
    std::uint32_t failed_count = 0;
    std::uint32_t failure_mask = 0;
    std::uint32_t last_step = 0;
    std::uint32_t stage_mask = 0;
    std::uint32_t observed_count = 0;
    float value_a = 0.0f;
    float value_b = 0.0f;
    float value_c = 0.0f;
};

struct imu_unit_state : unit_test_state
{
    bool ahrs_solved = false;
    float quaternion[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float total_yaw = 0.0f;
    bool imu_accel_chip_ok = false;
    bool imu_gyro_chip_ok = false;
    float imu_temperature = 0.0f;
    bool imu_temperature_ready = false;
    bool imu_temperature_control_ok = false;
    bool imu_calibrated = false;
    std::uint32_t imu_gyro_ready_count = 0;
    std::uint32_t imu_update_count = 0;
    // TEMP_IMU_DWT: remove after profiling.
    float temp_dwt_read_us = 0;
    float temp_dwt_quaternion_us = 0;
    float temp_dwt_tactical_us = 0;
    float temp_dwt_round_us = 0;
    float temp_dwt_round_min_us = 0;
    float temp_dwt_round_max_us = 0;
    float temp_dwt_round_avg_us = 0;
    std::uint32_t temp_dwt_count = 0;
    std::uint32_t imu_sample_error_count = 0;
    std::uint32_t imu_spi_read_error_count = 0;
    std::uint32_t imu_spi_write_error_count = 0;
    std::uint32_t imu_spi_lock_error_count = 0;

    // Shadow Tactical ESKF diagnostics.  These fields are intentionally kept
    // in the global demo_debug_instance for live debugger comparison.
    bool tactical_solved = false;
    float tactical_quaternion[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float tactical_yaw = 0.0f;
    float tactical_pitch = 0.0f;
    float tactical_roll = 0.0f;
    float tactical_gyro_bias[3] = {};
    float tactical_accel_weight = 1.0f;
    float tactical_accel_direction_error = 0.0f;
    float tactical_accel_magnitude_g = 0.0f;
    float tactical_gyro_magnitude_rad_s = 0.0f;
    float tactical_accel_magnitude_variance = 0.0f;
    float tactical_gyro_magnitude_variance = 0.0f;
    float tactical_impact_acc_delta_g = 0.0f;
    float tactical_impact_gyro_delta_rad_s = 0.0f;
    std::uint8_t tactical_motion_state = 0;
    std::uint8_t tactical_impact_state = 0;
    bool tactical_paddling = false;
    bool tactical_linear_motion = false;
    std::uint32_t tactical_update_count = 0;
    float tactical_earth_acceleration[3] = {};
    float tactical_heave_velocity = 0.0f;
    float tactical_heave_position = 0.0f;

    // Formal EKF minus Tactical ESKF, wrapped to [-pi, pi].
    float yaw_difference = 0.0f;
    float pitch_difference = 0.0f;
    float roll_difference = 0.0f;
};

struct ps2_unit_state : unit_test_state
{
    bool compiled_enabled = false;
    bool initialized = false;
    bool frame_valid = false;
    bool analog_mode = false;
    std::uint8_t last_status = protocol::status_code(types::status::not_configured);
    std::uint8_t controller_id = 0U;
    std::uint8_t raw[9] = {};
    std::uint32_t init_count = 0U;
    std::uint32_t poll_count = 0U;
    std::uint32_t valid_frame_count = 0U;
    std::uint32_t error_count = 0U;
    bool select = false;
    bool start = false;
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool l1 = false;
    bool l2 = false;
    bool r1 = false;
    bool r2 = false;
    bool triangle = false;
    bool circle = false;
    bool cross = false;
    bool square = false;
    bool l3 = false;
    bool r3 = false;
    float lx = 0.0f;
    float ly = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
};

struct remoter_unit_state : unit_test_state
{
    bool offline = true;
    std::uint32_t source = 0;
#if ENABLE_PS2_UART
    std::uint32_t ps2_link = 0;
    std::uint16_t ps2_buttons = 0;
    std::uint16_t ps2_raw_buttons = 0;
    std::uint16_t ps2_pressed = 0;
    std::uint16_t ps2_released = 0;
    std::uint16_t ps2_pressed_seen_mask = 0;
    std::uint16_t ps2_released_seen_mask = 0;
    bool ps2_mapping_match = false;
    bool ps2_mapping_pending = false;
    bool ps2_square = false;
    bool ps2_cross = false;
    bool ps2_circle = false;
    bool ps2_triangle = false;
    bool ps2_r1 = false;
    bool ps2_l1 = false;
    bool ps2_r2 = false;
    bool ps2_l2 = false;
    bool ps2_left = false;
    bool ps2_down = false;
    bool ps2_right = false;
    bool ps2_up = false;
    bool ps2_start = false;
    bool ps2_r3 = false;
    bool ps2_l3 = false;
    bool ps2_select = false;
    std::uint8_t ps2_raw_left_x = 127;
    std::uint8_t ps2_raw_left_y = 128;
    std::uint8_t ps2_raw_right_x = 127;
    std::uint8_t ps2_raw_right_y = 128;
    std::uint32_t ps2_frame_count = 0;
    std::uint32_t ps2_signal_count = 0;
    std::uint32_t ps2_last_signal_tick = 0;
    std::uint32_t ps2_raw_update_count = 0;
    std::uint32_t ps2_upper_event_count = 0;
    std::uint32_t ps2_button_event_count = 0;
    std::uint32_t ps2_last_button_latency_ticks = 0;
    std::uint32_t ps2_max_button_latency_ticks = 0;
    std::uint32_t ps2_frame_period_ticks = 0;
    std::uint32_t ps2_max_frame_period_ticks = 0;
#endif
    std::uint32_t left_sw = 0;
    std::uint32_t right_sw = 0;
    std::uint32_t last_left_sw = 0;
    std::uint32_t last_right_sw = 0;
    std::uint16_t key_bits = 0;
    std::uint16_t last_key_bits = 0;
    std::uint32_t update_count = 0;
    float right_x = 0.0f;
    float right_y = 0.0f;
    float left_x = 0.0f;
    float left_y = 0.0f;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    float command_x = 0.0f;
    float command_y = 0.0f;
    bool command_shoot = false;
    bool command_relax = true;
    std::uint32_t command_update_count = 0;
};

struct referee_ui_state : unit_test_state
{
    bool referee_online = false;
    bool ui_initialized = false;
    std::uint32_t referee_update_count = 0;
    std::uint16_t current_hp = 0;
    std::uint16_t max_hp = 0;
    std::uint16_t heat_now = 0;
    std::uint16_t power_buffer = 0;
};

struct debug_instance_type
{
    link_state can{};
    link_state usart{};
    link_state usb{};
    imu_unit_state imu_unit{};
    unit_test_state gpio_unit{};
    ps2_unit_state ps2_unit{};
    unit_test_state motor_unit{};
    remoter_unit_state remoter_unit{};
    referee_ui_state referee_ui{};
};

extern debug_instance_type& debug_instance;

void reset(link_state& state) noexcept;

} // namespace diagnose::debug

extern "C" diagnose::debug::debug_instance_type demo_debug_instance;
