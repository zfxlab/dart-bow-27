#include "launcher.hpp"
#include "dart_config.hpp"
#include "delay.hpp"
#include <cmath>

namespace dart::launcher
{
watch debug{};
namespace
{
TX_THREAD thread{};
alignas(8) std::uint8_t stack[2048]{};
msg::subscriber cmd_sub{}, sensor_sub{}, fdb_sub{};
state last_state = static_cast<state>(0xff);
prepare last_prep = static_cast<prepare>(0xff);
bool hand_locked = true;
bool fire_done{}, last_fire_done{};
delay retract_delay{}, trigger_delay{}, ready_delay{}, firing_delay{}, done_delay{}, torque_delay{};

void enter_prepare(const command& cmd)
{
    debug.fsm = state::preparing;
    debug.prep = prepare::retract;
    debug.current_slot = cmd.next_dart_slot;
}

void entry(ULONG)
{
    command cmd{};
    sensor_data sensor{};
    motor_fdb fdb{};
    motor_cmd ref{};
    launcher_status status{};
    for (;;)
    {
        (void)msg::read(cmd_sub, cmd);
        (void)msg::read(sensor_sub, sensor);
        (void)msg::read(fdb_sub, fdb);
        step(cmd, sensor, fdb, ref, status);
        (void)msg::publish(topics::motor, ref);
        (void)msg::publish(topics::launcher, status);
        tx_thread_sleep(cfg::launcher::period_ticks);
    }
}
}

float gantry_pos(slot selected) noexcept
{
    constexpr float pi = 3.14159265358979323846f;
    switch (selected)
    {
    case slot::slot_1: return -pi / 2.0f;
    case slot::slot_2: return 0.0f;
    case slot::slot_3: return pi / 2.0f;
    default: return pi;
    }
}

void step(const command& cmd, const sensor_data& sensor, const motor_fdb& fdb,
          motor_cmd& ref, launcher_status& status) noexcept
{
    // Keep the last position reference when idle/manual; other commands start at zero.
    const float syn_ref = ref.synbelt_pos;
    ref = {};
    ref.synbelt_pos = syn_ref;
    ref.yaw_spd = cmd.yaw * 0.1f;
    ref.string_l_tension_kg = cmd.tension_kg;
    ref.string_r_tension_kg = cmd.tension_kg;

    debug.force_error = std::fabs(sensor.string_l_force_kg) > cfg::launcher::force_error_kg ||
                        std::fabs(sensor.string_r_force_kg) > cfg::launcher::force_error_kg;
    debug.torque_error = torque_delay.stable(std::fabs(fdb.syn_tq_fdb) > cfg::launcher::syn_tq_error,
                                             cfg::launcher::syn_tq_error_ticks);
    if (debug.force_error || debug.torque_error)
        debug.fsm = state::error_stop;
    else if (debug.fsm != state::error_stop && debug.fsm != state::firing &&
             debug.fsm != state::preparing && debug.fsm != state::ready &&
             cmd.action == action::pre_tension)
        debug.fsm = state::pre_tension;
    else if (debug.fsm != state::error_stop && cmd.action == action::relax)
    {
        if ((debug.fsm != state::hand || hand_locked) && debug.fsm != state::firing)
            debug.fsm = state::idle;
    }
    else if (cmd.action == action::syn_adjust || cmd.action == action::string_adjust ||
             cmd.action == action::trigger_open || cmd.action == action::trigger_close ||
             cmd.action == action::yaw_adjust)
        debug.fsm = state::hand;

    // A transition inside the switch takes effect on the next control round.
    const state current = debug.fsm;
    const prepare prep = debug.prep;
    const bool changed = current != last_state;
    const bool prep_changed = changed || prep != last_prep;
    debug.gantry_missing = false;
    debug.left_ready = std::fabs(sensor.string_l_force_kg - cmd.tension_kg) <= cfg::launcher::tension_deadband;
    debug.right_ready = std::fabs(sensor.string_r_force_kg - cmd.tension_kg) <= cfg::launcher::tension_deadband;
    debug.syn_ready = std::fabs(fdb.syn_pos_fdb - cfg::launcher::syn_pos_5) <= cfg::launcher::syn_deadband;

    switch (current)
    {
    case state::hand:
        ref.trigger_release = !hand_locked;
        switch (cmd.action)
        {
        case action::syn_adjust: ref.synbelt_pos += cmd.rc_syn; break;
        case action::string_adjust:
            ref.string_l_spd = cmd.rc_string_l;
            ref.string_r_spd = cmd.rc_string_r;
            break;
        case action::fire: debug.fsm = state::firing; break;
        case action::prepare: enter_prepare(cmd); break;
        case action::trigger_close: hand_locked = true; ref.trigger_release = false; break;
        case action::trigger_open: hand_locked = false; ref.trigger_release = true; break;
        default: break;
        }
        break;
    case state::idle:
        if (cmd.action == action::prepare) enter_prepare(cmd);
        break;
    case state::pre_tension:
        ref.string_able = true;
        if (cmd.action == action::prepare) enter_prepare(cmd);
        else if (cmd.action == action::relax) debug.fsm = state::idle;
        break;
    case state::preparing:
        switch (prep)
        {
        case prepare::retract:
            ref.string_l_spd = sensor.string_l_force_kg > cfg::launcher::string_relax_min_kg ? cfg::launcher::string_relax_spd : 0.0f;
            ref.string_r_spd = sensor.string_r_force_kg > cfg::launcher::string_relax_min_kg ? cfg::launcher::string_relax_spd : 0.0f;
            if (retract_delay.reached(cfg::launcher::string_relax_ticks, prep_changed)) debug.prep = prepare::syn_1;
            break;
        case prepare::syn_1:
            ref.synbelt_pos = cfg::launcher::syn_pos_1;
            if (cmd.current_shot_number == 1) debug.prep = prepare::trigger_ready;
            else if (std::fabs(fdb.syn_pos_fdb - ref.synbelt_pos) <= cfg::launcher::syn_deadband) debug.prep = prepare::gantry_1;
            break;
        case prepare::gantry_1:
            ref.gantry_target_slot = debug.current_slot;
            debug.gantry_missing = !fdb.gantry_online;
            if (fdb.gantry_online && std::fabs(gantry_pos(debug.current_slot) - fdb.gantry_pos_fdb) <= cfg::launcher::gantry_deadband)
                debug.prep = prepare::syn_2;
            break;
        case prepare::syn_2:
            ref.gantry_target_slot = debug.current_slot;
            ref.synbelt_mode = mode::speed;
            ref.synbelt_spd = cfg::launcher::syn_slow_spd;
            ref.synbelt_pos = cfg::launcher::syn_pos_2;
            if (std::fabs(fdb.syn_pos_fdb - ref.synbelt_pos) <= cfg::launcher::syn_deadband)
            {
                ref.synbelt_spd = 0.0f;
                ref.synbelt_mode = mode::position;
                debug.prep = prepare::gantry_2;
            }
            break;
        case prepare::gantry_2:
            debug.gantry_missing = !fdb.gantry_online;
            if (fdb.gantry_online && std::fabs(gantry_pos(slot::none) - fdb.gantry_pos_fdb) <= cfg::launcher::gantry_deadband)
                debug.prep = prepare::trigger_ready;
            break;
        case prepare::trigger_ready:
            ref.synbelt_pos = cfg::launcher::syn_pos_3;
            if (trigger_delay.reached(std::fabs(fdb.syn_pos_fdb - ref.synbelt_pos) <= cfg::launcher::syn_deadband,
                                      cfg::launcher::trigger_ready_ticks, prep_changed))
                debug.prep = prepare::tension;
            break;
        case prepare::tension:
            ref.string_able = fdb.syn_pos_fdb >= cfg::launcher::syn_pos_1;
            ref.synbelt_pos = cfg::launcher::syn_pos_5;
            ref.yaw_spd = cmd.yaw;
            if (debug.left_ready && debug.right_ready && debug.syn_ready) debug.fsm = state::ready;
            break;
        }
        break;
    case state::ready:
        ref.string_able = true;
        if (changed) ready_delay.reset();
        if (cmd.action == action::fire && ready_delay.reached(cfg::launcher::ready_fire_ticks)) debug.fsm = state::firing;
        break;
    case state::firing:
        ref.trigger_release = true;
        fire_done = false;
        if (firing_delay.reached(cfg::launcher::firing_ticks, changed))
        {
            done_delay.reset();
            fire_done = true;
            debug.fsm = state::idle;
        }
        break;
    case state::error_stop:
        break;
    }

    status.current_state = static_cast<std::uint8_t>(debug.fsm);
    status.prepare_state = static_cast<std::uint8_t>(debug.prep);
    status.is_fire_finished = fire_done;
    status.last_fire_finished = last_fire_done;
    last_fire_done = fire_done;
    if (fire_done && done_delay.reached(cfg::launcher::fire_done_ticks)) fire_done = false;
    last_state = current;
    last_prep = prep;
    debug.cmd = cmd;
    debug.ref = ref;
    debug.fdb = fdb;
    debug.sensor = sensor;
    ++debug.loops;
}

types::status init() noexcept
{
    cmd_sub = msg::subscribe(topics::cmd);
    sensor_sub = msg::subscribe(topics::sensor);
    fdb_sub = msg::subscribe(topics::fdb);
    if (!cmd_sub.valid() || !sensor_sub.valid() || !fdb_sub.valid()) return types::status::error;
    return tx_thread_create(&thread, const_cast<CHAR*>("dart_launcher"), entry, 0,
        stack, sizeof(stack), cfg::launcher::thread_priority, cfg::launcher::thread_priority,
        TX_NO_TIME_SLICE, TX_AUTO_START) == TX_SUCCESS ? types::status::ok : types::status::error;
}
}
