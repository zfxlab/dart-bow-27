#include "imu_demo.hpp"

#include "ahrs.hpp"
#include "config.hpp"
#include "demo_debug.hpp"
#include "msg.hpp"
#include "robot_config.hpp"

#include "tx_api.h"

#include <cmath>
#include <cstring>

extern "C" {
diagnose::imu::dmimu_debug_state dmimu_demo_debug{};
float dmimu_yaw_debug=0.0f;
}

namespace diagnose::imu
{
namespace
{

enum stage : std::uint32_t
{
    service_initialized = 1U << 0U,
    subscriber_created = 1U << 1U,
    monitor_thread_started = 1U << 2U,
    data_received = 1U << 3U,
    quaternion_ok = 1U << 4U,
};

enum failure : std::uint32_t
{
    service_init_failed = 1U << 0U,
    subscribe_failed = 1U << 1U,
    thread_create_failed = 1U << 2U,
    data_timeout = 1U << 3U,
    quaternion_invalid = 1U << 4U,
};

constexpr std::uint32_t pass_stage_mask =
    service_initialized | subscriber_created | monitor_thread_started | data_received | quaternion_ok;

constexpr ULONG monitor_period_ticks = 1;
constexpr std::uint32_t warmup_ticks = 12000;

TX_THREAD monitor_thread{};
alignas(8) std::uint8_t monitor_stack[1024]{};
bool monitor_started = false;
msg::subscriber ahrs_sub{};
#if HAS_DMIMU
msg::subscriber dmimu_sub{};
#endif
ULONG started_at = 0;
std::uint32_t observed_count = 0;

bool quaternion_valid(const ::imu::state& data) noexcept
{
    const float norm =
        data.quaternion[0] * data.quaternion[0] +
        data.quaternion[1] * data.quaternion[1] +
        data.quaternion[2] * data.quaternion[2] +
        data.quaternion[3] * data.quaternion[3];
    return std::isfinite(norm) && norm > 0.8f && norm < 1.2f;
}

#if HAS_DMIMU
float quaternion_norm(const float quaternion[4]) noexcept
{
    return std::sqrt(quaternion[0] * quaternion[0] +
                     quaternion[1] * quaternion[1] +
                     quaternion[2] * quaternion[2] +
                     quaternion[3] * quaternion[3]);
}

void capture_dmimu_message(const ::imu::state& data) noexcept
{
    auto& debug = dmimu_demo_debug;
    debug.message_received = true;
    debug.online = data.online;
    ++debug.received_count;
    if (!data.online)
    {
        ++debug.offline_message_count;
    }
    debug.sequence = data.sequence;
    debug.received_tick = data.received_tick;
    debug.last_debug_tick = tx_time_get();
    debug.message_age_ticks = debug.last_debug_tick - data.received_tick;
    std::memcpy(debug.quaternion, data.quaternion, sizeof(debug.quaternion));
    debug.quaternion_norm = quaternion_norm(data.quaternion);
    debug.quaternion_valid = data.online && std::isfinite(debug.quaternion_norm) &&
                             debug.quaternion_norm > 0.8f && debug.quaternion_norm < 1.2f;
    debug.yaw = data.yaw;
    debug.pitch = data.pitch;
    debug.roll = data.roll;
    debug.gyro[0] = data.gyro_r;
    debug.gyro[1] = data.gyro_p;
    debug.gyro[2] = data.gyro_y;
    debug.accel[0] = data.accel_x;
    debug.accel[1] = data.accel_y;
    debug.accel[2] = data.accel_z;
    dmimu_yaw_debug = data.yaw;
}

void capture_dmimu_diagnostics() noexcept
{
    auto& debug = dmimu_demo_debug;
    const auto& service_diag = ahrs::dmimu_service::instance().diagnostics();
    debug.service_initialized = service_diag.initialized;
    debug.online = service_diag.online;
    debug.processed_frame_count = service_diag.processed_frame_count;
    debug.publish_count = service_diag.publish_count;
    debug.publish_error_count = service_diag.publish_error_count;
    debug.request_error_count = service_diag.request_error_count;
    debug.complete_snapshot_count = service_diag.device.complete_snapshot_count;
    debug.rx_queue_drop_count = service_diag.device.rx_queue_drop_count;
    debug.rx_invalid_length_count = service_diag.device.rx_invalid_length_count;
    debug.rx_invalid_frame_count = service_diag.device.rx_invalid_frame_count;
    debug.device_offline_event_count = service_diag.device.offline_event_count;
    debug.device_reconnect_count = service_diag.device.reconnect_count;
    debug.last_debug_tick = tx_time_get();
    if (debug.message_received)
    {
        debug.message_age_ticks = debug.last_debug_tick - debug.received_tick;
    }
}
#endif

#if HAS_AHRS
float wrap_angle(float angle) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    constexpr float two_pi = 2.0f * pi;
    while (angle > pi)
    {
        angle -= two_pi;
    }
    while (angle < -pi)
    {
        angle += two_pi;
    }
    return angle;
}

#endif

void sync_debug(const ::imu::state& data, std::uint32_t stages, bool timed_out) noexcept
{
    auto& state = diagnose::debug::debug_instance.imu_unit;
    const bool data_seen = (stages & data_received) != 0U;
    const bool valid_quaternion = data_seen && quaternion_valid(data);
    if (valid_quaternion)
    {
        stages |= quaternion_ok;
    }

    state.stage_mask = stages;
    state.last_step = stages;
    state.observed_count = observed_count;
    state.value_a = data.yaw;
    state.value_b = data.pitch;
    state.value_c = data.roll;
    state.ahrs_solved = data_seen;
    std::memcpy(state.quaternion, data.quaternion, sizeof(state.quaternion));
    state.yaw = data.yaw;
    state.pitch = data.pitch;
    state.roll = data.roll;
    state.total_yaw = data.total_yaw;

#if HAS_AHRS
    const auto& diagnostics = ahrs::service::instance().diagnostics();
    state.imu_accel_chip_ok = diagnostics.accel_chip_ok;
    state.imu_gyro_chip_ok = diagnostics.gyro_chip_ok;
    state.imu_temperature = diagnostics.temperature;
    state.imu_temperature_ready = diagnostics.temperature_ready;
    state.imu_temperature_control_ok =
        ahrs::service::instance().imu().diagnostics().temperature_control_ok;
    state.imu_calibrated = diagnostics.calibrated;
    state.imu_gyro_ready_count = diagnostics.gyro_ready_count;
    state.imu_update_count = diagnostics.update_count;
    // TEMP_IMU_DWT: remove after profiling.
    state.temp_dwt_read_us = diagnostics.temp_dwt_read_us;
    state.temp_dwt_quaternion_us = diagnostics.temp_dwt_quaternion_us;
    state.temp_dwt_tactical_us = diagnostics.temp_dwt_tactical_us;
    state.temp_dwt_round_us = diagnostics.temp_dwt_round_us;
    state.temp_dwt_round_min_us = diagnostics.temp_dwt_round_min_us;
    state.temp_dwt_round_max_us = diagnostics.temp_dwt_round_max_us;
    state.temp_dwt_round_avg_us = diagnostics.temp_dwt_round_avg_us;
    state.temp_dwt_count = diagnostics.temp_dwt_count;

    state.imu_sample_error_count = diagnostics.sample_error_count;
    state.imu_spi_read_error_count = diagnostics.imu_spi_read_error_count;
    state.imu_spi_write_error_count = diagnostics.imu_spi_write_error_count;
    state.imu_spi_lock_error_count = diagnostics.imu_spi_lock_error_count;

    const auto& tactical = diagnostics.tactical;
    state.tactical_solved = tactical.update_count != 0U;
    std::memcpy(state.tactical_quaternion, tactical.quaternion, sizeof(state.tactical_quaternion));
    state.tactical_yaw = tactical.yaw;
    state.tactical_pitch = tactical.pitch;
    state.tactical_roll = tactical.roll;
    std::memcpy(state.tactical_gyro_bias, tactical.gyro_bias, sizeof(state.tactical_gyro_bias));
    state.tactical_accel_weight = tactical.accel_weight;
    state.tactical_accel_direction_error = tactical.accel_direction_error;
    state.tactical_accel_magnitude_g = tactical.accel_magnitude_g;
    state.tactical_gyro_magnitude_rad_s = tactical.gyro_magnitude_rad_s;
    state.tactical_accel_magnitude_variance = tactical.accel_magnitude_variance;
    state.tactical_gyro_magnitude_variance = tactical.gyro_magnitude_variance;
    state.tactical_impact_acc_delta_g = tactical.impact_acc_delta_g;
    state.tactical_impact_gyro_delta_rad_s = tactical.impact_gyro_delta_rad_s;
    state.tactical_motion_state = tactical.motion_state;
    state.tactical_impact_state = tactical.impact_state;
    state.tactical_paddling = tactical.paddling;
    state.tactical_linear_motion = tactical.linear_motion;
    state.tactical_update_count = tactical.update_count;
    std::memcpy(state.tactical_earth_acceleration, tactical.earth_acceleration,
                sizeof(state.tactical_earth_acceleration));
    state.tactical_heave_velocity = tactical.heave_velocity;
    state.tactical_heave_position = tactical.heave_position;
    state.yaw_difference = wrap_angle(data.yaw - tactical.yaw);
    state.pitch_difference = wrap_angle(data.pitch - tactical.pitch);
    state.roll_difference = wrap_angle(data.roll - tactical.roll);

#endif

    state.failure_mask = 0;
    if ((stages & service_initialized) == 0U)
    {
        state.failure_mask |= service_init_failed;
    }
    if ((stages & subscriber_created) == 0U)
    {
        state.failure_mask |= subscribe_failed;
    }
    if ((stages & monitor_thread_started) == 0U)
    {
        state.failure_mask |= thread_create_failed;
    }
    if (timed_out && !data_seen)
    {
        state.failure_mask |= data_timeout;
    }
    if (data_seen && !valid_quaternion)
    {
        state.failure_mask |= quaternion_invalid;
    }

    state.failed_count = state.failure_mask == 0U ? 0U : 1U;
    state.passed = (stages & pass_stage_mask) == pass_stage_mask && state.failure_mask == 0U;
    state.passed_count = state.passed ? state.total_count : 0U;
}

void monitor_entry(ULONG /*arg*/)
{
    ::imu::state data{};
#if HAS_DMIMU
    ::imu::state dmimu_data{};
#endif
    std::uint32_t stages = service_initialized | subscriber_created | monitor_thread_started;
    for (;;)
    {
        if (msg::read(ahrs_sub, data) == types::status::ok)
        {
            stages |= data_received;
            ++observed_count;
        }
#if HAS_DMIMU
        if (msg::available(dmimu_sub))
        {
            if (msg::read(dmimu_sub, dmimu_data) == types::status::ok)
            {
                capture_dmimu_message(dmimu_data);
            }
            else
            {
                ++dmimu_demo_debug.read_error_count;
            }
        }
        capture_dmimu_diagnostics();
#endif
        sync_debug(data, stages, (tx_time_get() - started_at) > warmup_ticks);
        tx_thread_sleep(monitor_period_ticks);
    }
}

} // namespace

void start() noexcept
{
    auto& state = diagnose::debug::debug_instance.imu_unit;
    state = {};
    state.started = true;
    state.total_count = 5U;
    observed_count = 0;
    started_at = tx_time_get();
    dmimu_demo_debug = {};
#if HAS_DMIMU
    dmimu_demo_debug.compiled_enabled = true;
#endif

#if HAS_AHRS
    ahrs::config cfg{};
    cfg.imu_offset_x = params::ahrs::imu_offset_x;
    cfg.imu_thread_priority = params::ahrs::imu_thread_priority;
    cfg.temp_thread_priority = params::ahrs::temp_thread_priority;
    cfg.target_temp = params::ahrs::target_temp;
    cfg.temperature_control_enabled = true;

    if (ahrs::service::instance().init(cfg) != types::status::ok)
    {
        state.failure_mask = service_init_failed;
        state.failed_count = 1U;
        state.passed = false;
        return;
    }

#endif

#if HAS_DMIMU
    ::imu::dmimu::config dmimu_cfg{};
    dmimu_cfg.transport = robot::imu::dmimu;
    dmimu_cfg.runtime.communication_mode =
        params::dmimu::mode == params::dmimu::communication_mode::active
            ? ::imu::dmimu::mode::active
            : ::imu::dmimu::mode::request;
    dmimu_cfg.runtime.offline_timeout_ticks = params::dmimu::offline_timeout_ticks;

    ahrs::dmimu_service_config dmimu_service_cfg{};
    dmimu_service_cfg.thread_priority = params::dmimu::thread_priority;
    dmimu_service_cfg.receive_wait_ticks = params::dmimu::receive_wait_ticks;
    dmimu_service_cfg.request_period_ticks = params::dmimu::request_period_ticks;

    if (ahrs::dmimu_service::instance().init(dmimu_cfg, dmimu_service_cfg) !=
        types::status::ok)
    {
        state.failure_mask = service_init_failed;
        state.failed_count = 1U;
        state.passed = false;
        return;
    }
    dmimu_demo_debug.service_initialized = true;

    dmimu_sub = msg::subscribe(ahrs::dmimu_service::instance().output());
    dmimu_demo_debug.subscriber_created = dmimu_sub.valid();
    if (!dmimu_sub.valid())
    {
        state.failure_mask = subscribe_failed;
        state.failed_count = 1U;
        state.passed = false;
        return;
    }
#endif

#if HAS_AHRS
    ahrs_sub = msg::subscribe(ahrs::service::instance().output());
#else
    ahrs_sub = msg::subscribe(ahrs::dmimu_service::instance().output());
#endif
    if (!ahrs_sub.valid())
    {
        state.failure_mask = subscribe_failed;
        state.failed_count = 1U;
        return;
    }

#if HAS_AHRS
    constexpr auto monitor_priority = params::ahrs::imu_thread_priority + 1U;
#else
    constexpr auto monitor_priority = params::dmimu::thread_priority + 1U;
#endif
    if (!monitor_started)
    {
        if (tx_thread_create(&monitor_thread, const_cast<CHAR*>("imu_unit_mon"), monitor_entry, 0,
                             monitor_stack, sizeof(monitor_stack), monitor_priority,
                             monitor_priority, TX_NO_TIME_SLICE, TX_AUTO_START) == TX_SUCCESS)
        {
            monitor_started = true;
        }
        else
        {
            state.failure_mask = thread_create_failed;
            state.failed_count = 1U;
            return;
        }
    }

    sync_debug({}, service_initialized | subscriber_created | monitor_thread_started, false);
}

} // namespace diagnose::imu
