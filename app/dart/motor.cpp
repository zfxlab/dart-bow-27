#include "dart_motor.hpp"
#include "motor/motor_ops.hpp"

#include "messages.hpp"
#include "dart_config.hpp"
#include "robot_config.hpp"
#include "dmmotors.hpp"
#include "motorservice.hpp"
#include "bsp_can.hpp"
#include "bsp_pwm.hpp"
#include "pid.hpp"
#include "constants.hpp"
#include "constrain.hpp"
#include "tx_api.h"

#include <cmath>

namespace dart::motor
{
namespace
{
constexpr std::uint8_t old_pid_mode =
    static_cast<std::uint8_t>(control::pid_mode::position) |
    static_cast<std::uint8_t>(control::pid_mode::integral_limit) |
    static_cast<std::uint8_t>(control::pid_mode::trapezoid_integral);

pid_settings settings(float kp, float ki, float kd, float max_out, float max_iout)
{
    return {kp, ki, kd, max_out, max_iout, old_pid_mode, false, false, 0.0f};
}
} // namespace

volatile watch debug{};
volatile pid_tuning pidtuning{
    settings(cfg::motor::syn_kp, cfg::motor::syn_ki, cfg::motor::syn_kd,
             cfg::motor::syn_max_out, cfg::motor::syn_max_iout),
    settings(cfg::motor::string_kp, cfg::motor::string_ki, cfg::motor::string_kd,
             cfg::motor::string_max_out, cfg::motor::string_max_iout),
    settings(cfg::motor::string_kp, cfg::motor::string_ki, cfg::motor::string_kd,
             cfg::motor::string_max_out, cfg::motor::string_max_iout)};

namespace
{
TX_THREAD thread{};
alignas(8) std::uint8_t stack[2048]{};
bool started = false;
msg::subscriber motor_sub{}, sensor_sub{};
motors::motor_service service{};
motors::dm4310 synbelt{robot::motors::synbelt};
motors::dm8009p yaw{robot::motors::yaw};

// Preserve the old combined mode. Both the old and current shared PID only
// calculate ordinary D for mode == position; kd=10 is dormant with mode 0x45.
// pidtuning.mode is explicit so a tuning session can deliberately change it.
control::pid syn_pid{cfg::motor::syn_kp, cfg::motor::syn_ki, cfg::motor::syn_kd,
                     cfg::motor::syn_max_out, cfg::motor::syn_max_iout,
                     static_cast<control::pid_mode>(old_pid_mode)};
control::pid string_l_pid{cfg::motor::string_kp, cfg::motor::string_ki, cfg::motor::string_kd,
                          cfg::motor::string_max_out, cfg::motor::string_max_iout,
                          static_cast<control::pid_mode>(old_pid_mode)};
control::pid string_r_pid{cfg::motor::string_kp, cfg::motor::string_ki, cfg::motor::string_kd,
                          cfg::motor::string_max_out, cfg::motor::string_max_iout,
                          static_cast<control::pid_mode>(old_pid_mode)};

struct string_feedback
{
    std::uint32_t rx_count{};
    float speed_rpm{}, current_ma{};
};
string_feedback string_l_fdb{}, string_r_fdb{};

void read_string_frame(const bsp::can::rx_frame& frame, string_feedback& fdb)
{
    if (frame.len < 4U)
    {
        return;
    }
    // The old X_V2 feedback begins with function, sign, magnitude (big endian).
    // Feedback was not polled by oldframe; counters remain zero if the drive
    // has not been configured to return data. Zero is not an online claim.
    const auto value = static_cast<std::uint16_t>((frame.data[2] << 8U) | frame.data[3]);
    if (frame.data[0] == 0x35U)
    {
        fdb.speed_rpm = static_cast<float>(value) * 0.1f * (frame.data[1] == 0U ? 1.0f : -1.0f);
    }
    else if (frame.data[0] == 0x27U)
    {
        fdb.current_ma = static_cast<float>(value);
    }
    ++fdb.rx_count;
}

void on_string_frame(bsp::can::bus bus, const bsp::can::rx_frame& frame)
{
    if (frame.id_kind != bsp::can::id_type::extended ||
        frame.format != bsp::can::bus_type::classic)
    {
        return;
    }
    const std::uint32_t address = (frame.id >> 8U) & 0xFFU;
    if (bus == robot::motors::string_l.can_bus && address == robot::motors::string_l.can_id)
    {
        read_string_frame(frame, string_l_fdb);
    }
    else if (bus == robot::motors::string_r.can_bus && address == robot::motors::string_r.can_id)
    {
        read_string_frame(frame, string_r_fdb);
    }
}

types::status init_strings()
{
    const auto callback = bsp::can::rx_callback::bind<&on_string_frame>();
    auto status = bsp::can::register_rx_callback(robot::motors::string_l.can_bus, callback);
    if (status != types::status::ok) return status;
    status = bsp::can::init(robot::motors::string_l.can_bus);
    if (status != types::status::ok) return status;
    if (robot::motors::string_r.can_bus != robot::motors::string_l.can_bus)
    {
        status = bsp::can::register_rx_callback(robot::motors::string_r.can_bus, callback);
        if (status != types::status::ok) return status;
        status = bsp::can::init(robot::motors::string_r.can_bus);
    }
    return status;
}

// This old drive uses Classic CAN extended-ID fragments and command 0xC6.
// The existing XV2 device uses another transport and lacks this current limit.
// Keep this one missing operation local; motor identities still come from JSON.
void send_string(const motors::config& motor, std::uint8_t positive_dir, float speed_rpm)
{
    const auto frames = detail::make_string_frames(static_cast<std::uint8_t>(motor.can_id),
        positive_dir, speed_rpm, cfg::motor::string_accel_rpm_s, cfg::motor::string_max_current_ma);
    const auto first_status = bsp::can::transmit(motor.can_bus, frames.first.id, frames.first.data, frames.first.len);
    const auto last_status = bsp::can::transmit(motor.can_bus, frames.last.id, frames.last.data, frames.last.len);
    if (first_status != types::status::ok || last_status != types::status::ok)
    {
        ++debug.string_tx_errors;
    }
}

void pwm_result(types::status status)
{
    if (status != types::status::ok) ++debug.pwm_errors;
}

void pulse(std::uint32_t width_us)
{
    pwm_result(bsp::pwm::start(app::pwm::trigger));
    pwm_result(bsp::pwm::set_pulse_width_us(app::pwm::trigger, width_us));
    debug.trigger_pwm_stopped = false;
}

enum class trigger_phase : std::uint8_t { idle, opening, resetting };
trigger_phase trigger_state = trigger_phase::idle;
ULONG trigger_tick{};
bool last_trigger_release{};

void update_trigger(bool release, ULONG now)
{
    if (release && !last_trigger_release && trigger_state == trigger_phase::idle)
    {
        pulse(cfg::motor::trigger_open_us);
        trigger_tick = now;
        trigger_state = trigger_phase::opening;
    }
    if (trigger_state == trigger_phase::opening &&
        static_cast<ULONG>(now - trigger_tick) >= cfg::motor::trigger_hold_ticks)
    {
        pulse(cfg::motor::trigger_idle_us);
        trigger_tick = now;
        trigger_state = trigger_phase::resetting;
    }
    else if (trigger_state == trigger_phase::resetting &&
             static_cast<ULONG>(now - trigger_tick) >= cfg::motor::trigger_hold_ticks)
    {
        pwm_result(bsp::pwm::stop(app::pwm::trigger));
        debug.trigger_pwm_stopped = true;
        trigger_state = trigger_phase::idle;
    }
    last_trigger_release = release;
    debug.trigger_release = release;
    debug.trigger_state = static_cast<std::uint8_t>(trigger_state);
}

float update_pid(control::pid& pid, volatile pid_settings& tuning,
                 float ref, float fdb, volatile loop_watch& watch)
{
    const auto mode = static_cast<control::pid_mode>(tuning.mode);
    if (tuning.ref_override) ref = tuning.ref;
    if (tuning.reset || pid.mode != mode)
    {
        pid.reset_state(ref, fdb);
        tuning.reset = false;
    }
    pid.mode = mode;
    pid.tune(tuning.kp, tuning.ki, tuning.kd);
    pid.max_out = tuning.max_out;
    pid.max_iout = tuning.max_iout;
    pid.ref = ref;
    pid.fdb = fdb;
    pid.update();
    watch.ref = ref;
    watch.fdb = fdb;
    watch.err = ref - fdb;
    watch.out = pid.result;
    return pid.result;
}

float manual_string(float ref)
{
    return std::fabs(ref) > cfg::motor::string_deadband
        ? math::limit_abs(ref, 1.0f) * cfg::motor::string_manual_max_rpm : 0.0f;
}

void entry(ULONG)
{
    debug.init_phase = 1U;
    if (!service.register_motor(synbelt) || !service.register_motor(yaw))
    {
        debug.init_status = types::status::error;
        return;
    }
    debug.init_status = init_strings();
    if (debug.init_status != types::status::ok) return;
    debug.init_status = bsp::pwm::init(app::pwm::trigger);
    if (debug.init_status != types::status::ok) return;
    pulse(cfg::motor::trigger_idle_us);

    debug.init_phase = 2U;
    // Preserve oldframe's explicit initial speed command. The first control
    // cycle below replaces it; this is exposed in JSON for bench review.
    synbelt.set_velocity(cfg::motor::initial_syn_spd);
    yaw.set_velocity(0.0f);
    debug.init_status = service.start();
    debug.syn_enable_failed = synbelt.enablefailed;
    debug.yaw_enable_failed = yaw.enablefailed;
    service.send_control();

    motor_cmd cmd{};
    sensor_data sensor{};
    motor_fdb output{};
    detail::position_state syn_position{};
    std::uint32_t last_syn_rx{};
    ULONG last_alive_tick = tx_time_get();
    debug.init_phase = 3U;
    for (;;)
    {
        (void)msg::read(motor_sub, cmd);
        (void)msg::read(sensor_sub, sensor);
        const ULONG now = tx_time_get();
        if (static_cast<ULONG>(now - last_alive_tick) >= cfg::motor::alive_check_ticks)
        {
            service.alive_check();
            last_alive_tick = now;
        }

        // Public motor feedback is written in CAN IRQs. Copy a small snapshot
        // while IRQs are masked, then run all control and messaging in thread.
        motors::feedback syn_fdb{}, yaw_fdb{};
        string_feedback left{}, right{};
        std::uint32_t syn_rx{}, yaw_rx{};
        const UINT irq_state = tx_interrupt_control(TX_INT_DISABLE);
        syn_fdb = synbelt.get_feedback();
        yaw_fdb = yaw.get_feedback();
        syn_rx = synbelt.alive;
        yaw_rx = yaw.alive;
        left = string_l_fdb;
        right = string_r_fdb;
        tx_interrupt_control(irq_state);

        if (syn_rx != last_syn_rx)
        {
            output.syn_pos_fdb = detail::unfold_position(syn_position, syn_fdb.position);
            last_syn_rx = syn_rx;
        }
        output.syn_tq_fdb = syn_fdb.torque;
        output.yaw_pos_fdb = yaw_fdb.position;
        output.syn_online = synbelt.status() == motors::state::online;
        output.yaw_online = yaw.status() == motors::state::online;
        // Gantry registration was commented out in oldframe. Do not invent a
        // successful position or online state for an absent active consumer.
        output.gantry_online = false;

        update_trigger(cmd.trigger_release, now);
        float left_rpm = manual_string(cmd.string_l_spd);
        float right_rpm = manual_string(cmd.string_r_spd);
        if (cmd.string_able)
        {
            left_rpm = -update_pid(string_l_pid, pidtuning.string_l,
                cmd.string_l_tension_kg, sensor.string_l_force_kg, debug.string_l);
            right_rpm = -update_pid(string_r_pid, pidtuning.string_r,
                cmd.string_r_tension_kg, sensor.string_r_force_kg, debug.string_r);
        }
        else
        {
            debug.string_l.ref = cmd.string_l_tension_kg;
            debug.string_l.fdb = sensor.string_l_force_kg;
            debug.string_l.err = cmd.string_l_tension_kg - sensor.string_l_force_kg;
            debug.string_l.out = 0.0f;
            debug.string_r.ref = cmd.string_r_tension_kg;
            debug.string_r.fdb = sensor.string_r_force_kg;
            debug.string_r.err = cmd.string_r_tension_kg - sensor.string_r_force_kg;
            debug.string_r.out = 0.0f;
        }
        // Retain the existing oldframe force ceiling; no new recovery logic.
        if (sensor.string_l_force_kg > cfg::motor::string_force_limit_kg && left_rpm < 0.0f) left_rpm = 0.0f;
        if (sensor.string_r_force_kg > cfg::motor::string_force_limit_kg && right_rpm < 0.0f) right_rpm = 0.0f;
        send_string(robot::motors::string_l, cfg::motor::string_l_positive_dir, left_rpm);
        send_string(robot::motors::string_r, cfg::motor::string_r_positive_dir, right_rpm);

        float syn_spd = 0.0f;
        if (cmd.synbelt_mode == mode::position)
        {
            syn_spd = update_pid(syn_pid, pidtuning.syn, cmd.synbelt_pos, output.syn_pos_fdb, debug.syn);
        }
        else if (cmd.synbelt_mode == mode::speed)
        {
            syn_spd = cmd.synbelt_spd;
        }
        synbelt.set_velocity(syn_spd);
        yaw.set_velocity(cmd.yaw_spd);
        service.send_control();
        (void)msg::publish(topics::fdb, output);

        ++debug.loop_count;
        debug.syn_rx_count = syn_rx;
        debug.yaw_rx_count = yaw_rx;
        debug.syn_error = syn_fdb.error_code;
        debug.yaw_error = yaw_fdb.error_code;
        debug.syn_online = output.syn_online;
        debug.yaw_online = output.yaw_online;
        debug.gantry_online = false;
        debug.syn_rounds = syn_position.rounds;
        debug.syn_raw_pos = syn_fdb.position;
        debug.syn_spd_ref = syn_spd;
        debug.syn_spd_fdb = syn_fdb.velocity;
        debug.syn_tq_fdb = syn_fdb.torque;
        debug.yaw_spd_ref = cmd.yaw_spd;
        debug.yaw_spd_fdb = yaw_fdb.velocity;
        debug.yaw_pos_fdb = yaw_fdb.position;
        debug.string_able = cmd.string_able;
        debug.string_l_spd_ref = left_rpm;
        debug.string_r_spd_ref = right_rpm;
        debug.string_l_spd_fdb = left.speed_rpm;
        debug.string_r_spd_fdb = right.speed_rpm;
        debug.string_l_current_ma = left.current_ma;
        debug.string_r_current_ma = right.current_ma;
        debug.string_l_rx_count = left.rx_count;
        debug.string_r_rx_count = right.rx_count;
        tx_thread_sleep(cfg::motor::period_ticks);
    }
}
} // namespace

types::status init() noexcept
{
    if (started) return types::status::ok;
    motor_sub = msg::subscribe(topics::motor);
    sensor_sub = msg::subscribe(topics::sensor);
    if (!motor_sub.valid() || !sensor_sub.valid()) return types::status::error;
    const UINT status = tx_thread_create(&thread, const_cast<CHAR*>("dart_motor"), entry, 0U,
        stack, sizeof(stack), cfg::motor::thread_priority, cfg::motor::thread_priority,
        TX_NO_TIME_SLICE, TX_AUTO_START);
    if (status != TX_SUCCESS) return types::status::error;
    started = true;
    return types::status::ok;
}
} // namespace dart::motor
