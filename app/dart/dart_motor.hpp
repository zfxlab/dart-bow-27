#pragma once

#include "usertypes.hpp"
#include <cstdint>

namespace dart::motor
{
// Editable in the debugger. ref_override only replaces an already active loop's
// reference; it does not switch the launcher into tension or position control.
struct pid_settings
{
    float kp{}, ki{}, kd{}, max_out{}, max_iout{};
    std::uint8_t mode{};
    bool ref_override{}, reset{};
    float ref{};
};
struct pid_tuning
{
    pid_settings syn{}, string_l{}, string_r{};
};
struct loop_watch
{
    float ref{}, fdb{}, err{}, out{};
};
struct watch
{
    std::uint32_t loop_count{}, syn_rx_count{}, yaw_rx_count{};
    std::uint32_t string_l_rx_count{}, string_r_rx_count{};
    std::uint32_t string_tx_errors{}, pwm_errors{};
    types::status init_status = types::status::not_configured;
    std::uint8_t init_phase{}, syn_error{}, yaw_error{}, trigger_state{};
    bool syn_online{}, yaw_online{}, gantry_online{};
    bool syn_enable_failed{}, yaw_enable_failed{}, string_able{};
    bool trigger_release{}, trigger_pwm_stopped{};
    std::int32_t syn_rounds{};
    float syn_raw_pos{}, syn_spd_ref{}, syn_spd_fdb{}, syn_tq_fdb{};
    float yaw_spd_ref{}, yaw_spd_fdb{}, yaw_pos_fdb{};
    float string_l_spd_ref{}, string_r_spd_ref{};
    float string_l_spd_fdb{}, string_r_spd_fdb{};
    float string_l_current_ma{}, string_r_current_ma{};
    loop_watch syn{}, string_l{}, string_r{};
};

extern volatile watch debug;
extern volatile pid_tuning pidtuning;

// Creates the application motor thread. DM enable waits run in that thread.
types::status init() noexcept;
} // namespace dart::motor
