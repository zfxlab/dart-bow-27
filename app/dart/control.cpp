#include "control.hpp"
#include "dart_config.hpp"
#include "host.hpp"
#include "vision.hpp"
#include "referee.hpp"
#include <cmath>
#include <iterator>

namespace dart::control
{
watch debug{};
namespace
{
TX_THREAD thread{};
alignas(8) std::uint8_t stack[3072]{};
msg::subscriber rc_sub{}, launcher_sub{}, vision_sub{}, fdb_sub{}, sensor_sub{};
referee_data referee_latest{};

void on_referee(const referee::packet_store& packets, const referee::update_info& update)
{
    if (!update.is_known_packet) return;
    // The callback belongs to the referee thread. A short critical copy avoids
    // a mutex or a borrowed packet reference in the control thread.
    const UINT irq = tx_interrupt_control(TX_INT_DISABLE);
    switch (update.command_id)
    {
    case RefereeID::GameStatus:
        referee_latest.game_status = packets.game_status.Game_progress;
        break;
    case RefereeID::DartInfo:
        referee_latest.shooting_remaining_time = packets.dart_info.dart_remaining_time;
        referee_latest.chosen_target = (packets.dart_info.dart_info >> 6U) & 0x07U;
        break;
    case RefereeID::DartClientCmd:
        referee_latest.launch_station_status = packets.dart_client_command.dart_launch_opening_status;
        break;
    default: break;
    }
    tx_interrupt_control(irq);
}

void load_calibration()
{
    for (std::size_t i = 0; i < std::size(cfg::calibration); ++i)
        state.config.dart[i + 1] = cfg::calibration[i];
    for (int i = 0; i < 4; ++i) state.config.sequence[i] = cfg::sequence[i];
    state.config.pre_tension_kg = cfg::pre_tension_kg;
    state.set_table(cfg::distance_table, static_cast<std::uint16_t>(std::size(cfg::distance_table)));
    state.update_dart_id();
}

void update_aim(const vision_rx& vision)
{
    const int id = state.runtime.current_dart_id;
    const dart_param& param = state.config.dart[id >= 1 && id <= 16 ? id : 0];
    if (state.runtime.referee.chosen_target == 0)
        state.runtime.current_aim_target = {param.yaw_offset, param.tension_kg_outpost};
    else if (cfg::system::fixed_aim)
        state.runtime.current_aim_target = {param.yaw_offset, param.tension_kg_base};
    else
    {
        const auto aim = state.lookup_aim(id, vision.distance);
        state.runtime.current_aim_target = {aim.yaw_offset, aim.tension_kg};
    }
}

void entry(ULONG)
{
    remoter::state rc{};
    launcher_status launcher{};
    vision_rx vision{};
    motor_fdb fdb{};
    sensor_data sensor{};
    for (;;)
    {
        (void)msg::read(rc_sub, rc);
        (void)msg::read(launcher_sub, launcher);
        (void)msg::read(vision_sub, vision);
        (void)msg::read(fdb_sub, fdb);
        (void)msg::read(sensor_sub, sensor);
        const UINT irq = tx_interrupt_control(TX_INT_DISABLE);
        const referee_data referee = referee_latest;
        tx_interrupt_control(irq);
        auto& runtime = state.runtime;
        runtime.referee.last_launch_station_status = runtime.referee.launch_station_status;
        runtime.referee.game_status = referee.game_status;
        runtime.referee.shooting_remaining_time = referee.shooting_remaining_time;
        runtime.referee.chosen_target = referee.chosen_target;
        runtime.referee.launch_station_status = referee.launch_station_status;
        if (cfg::system::target_override >= 0)
            runtime.referee.chosen_target = static_cast<std::uint8_t>(cfg::system::target_override);

        state.update_shot(&launcher);
        state.update_door(&vision);
        state.update_prepare();
        runtime.auto_aim.running = false;
        state.update_fired(&launcher);
        state.update_dart_id();
        host::poll(state, launcher);

        const bool auto_request = !rc.offline && rc.left_sw == remoter::sw_state::up && rc.right_sw == remoter::sw_state::up;
        if (cfg::system::auto_on_boot)
            runtime.auto_aim.enable = !(!rc.offline && !auto_request);
        else if (!rc.offline)
            runtime.auto_aim.enable = auto_request;
        const bool auto_control = runtime.auto_aim.enable && (rc.offline || auto_request);

        // oldframe commented out its game gate assignment. Keep that default
        // explicitly configurable rather than silently enabling auto firing.
        if (!cfg::system::legacy_game_gate)
            runtime.game_status_stable = referee.game_status == 4 ? 4 : 0;
        update_aim(vision);
        command cmd{};
        cmd.tension_kg = runtime.current_aim_target.tension_kg;
        cmd.current_shot_number = static_cast<std::uint8_t>(runtime.current_shot_number);
        cmd.next_dart_slot = state.prepare_slot();
        if (rc.offline && !auto_control)
            cmd.action = action::relax;
        else if (!auto_control)
            map_remote(rc, cmd);
        else if (runtime.game_status_stable == 4 && runtime.auto_aim.autoaim_allow && runtime.fired_count_this_open < 2)
            auto_command(vision, cmd);
        else
        {
            cmd.action = action::pre_tension;
            cmd.tension_kg = state.config.pre_tension_kg;
        }

        vision_tx tx{};
        tx.header = 0x5a;
        tx.target_id = runtime.referee.chosen_target;
        tx.dart_number = static_cast<std::uint8_t>(runtime.current_dart_id);
        tx.offset = runtime.current_aim_target.yaw_offset;
        tx.yaw = fdb.yaw_pos_fdb;
        vision::log_frame log{};
        log.state = launcher.current_state;
        log.prepare_state = launcher.prepare_state;
        log.launch_station_status = runtime.referee.launch_station_status;
        log.is_fire_finished = launcher.is_fire_finished;
        log.fired_count_this_open = static_cast<std::uint8_t>(runtime.fired_count_this_open);
        log.current_shot_number = static_cast<std::uint8_t>(runtime.current_shot_number);
        log.current_dart_id = static_cast<std::uint8_t>(runtime.current_dart_id);
        log.door_status = static_cast<std::uint8_t>(runtime.vision_door_status);
        log.vision_light_detected = vision.light_detected;
        log.vision_stable_state = vision.stable_state;
        log.autoaim_allow = runtime.auto_aim.autoaim_allow;
        log.string_l_force_kg = sensor.string_l_force_kg;
        log.string_r_force_kg = sensor.string_r_force_kg;
        vision::send(tx, log);
        state.update_history(&launcher);
        (void)msg::publish(topics::cmd, cmd);
        (void)msg::publish(topics::vision_out, tx);
        (void)msg::publish(topics::referee, referee);
        debug.cmd = cmd;
        debug.rc = rc;
        debug.vision = vision;
        debug.referee = referee;
        debug.auto_control = auto_control;
        debug.game_gate_closed = runtime.game_status_stable != 4;
        ++debug.loops;
        tx_thread_sleep(cfg::system::period_ticks);
    }
}
}

void map_remote(const remoter::state& rc, command& cmd) noexcept
{
    using sw = remoter::sw_state;
    if (rc.left_sw == sw::low)
    {
        if (rc.right_sw == sw::low) cmd.action = action::relax;
        else if (rc.right_sw == sw::mid)
        {
            cmd.action = action::syn_adjust;
            cmd.rc_syn = -rc.right_y * cfg::system::syn_manual_scale;
        }
        else
        {
            cmd.action = action::string_adjust;
            cmd.rc_string_l = rc.left_y;
            cmd.rc_string_r = rc.right_y;
        }
    }
    else if (rc.left_sw == sw::mid)
    {
        cmd.yaw = rc.right_x;
        if (rc.right_sw == sw::low)
            cmd.action = std::fabs(rc.left_x) > cfg::system::trigger_threshold ? action::trigger_open : action::trigger_close;
        else if (rc.right_sw == sw::mid) cmd.action = action::prepare;
        else cmd.action = action::fire;
    }
}

void auto_command(const vision_rx& vision, command& cmd) noexcept
{
    auto& runtime = state.runtime;
    runtime.auto_aim.running = true;
    runtime.auto_aim.yaw_ok = false;
    if (runtime.current_shot_number < 1 || runtime.current_shot_number > 4)
    {
        cmd.action = action::relax;
        cmd.yaw = 0;
        return;
    }
    cmd.action = action::prepare;
    if (vision.yaw == 666.0f) return;
    const float error = std::fabs(vision.yaw);
    if (error > cfg::system::yaw_fast_threshold) cmd.yaw = std::copysign(cfg::system::yaw_fast_spd, vision.yaw);
    else if (error > cfg::system::yaw_mid_threshold) cmd.yaw = std::copysign(cfg::system::yaw_mid_spd, vision.yaw);
    else if (error > cfg::system::yaw_deadband) cmd.yaw = std::copysign(cfg::system::yaw_slow_spd, vision.yaw);
    else if (error <= cfg::system::yaw_deadband) runtime.auto_aim.yaw_ok = true;
    if (runtime.vision_door_status == door_state::open && runtime.fired_count_this_open < 2 &&
        runtime.auto_aim.yaw_ok && vision.stable_state == 1)
        cmd.action = action::fire;
}

types::status init() noexcept
{
    load_calibration();
    auto result = remoter::service::instance().init();
    if (result != types::status::ok) return result;
    rc_sub = msg::subscribe(remoter::service::instance().output());
    launcher_sub = msg::subscribe(topics::launcher);
    vision_sub = msg::subscribe(topics::vision);
    fdb_sub = msg::subscribe(topics::fdb);
    sensor_sub = msg::subscribe(topics::sensor);
    if (!rc_sub.valid() || !launcher_sub.valid() || !vision_sub.valid() || !fdb_sub.valid() || !sensor_sub.valid())
        return types::status::error;
    referee::config referee_cfg{};
    referee_cfg.thread_priority = params::referee::thread_priority;
    referee_cfg.on_update_callback = referee::update_callback::bind<&on_referee>();
    result = referee::service::instance().init(referee_cfg);
    if (result != types::status::ok) return result;
    return tx_thread_create(&thread, const_cast<CHAR*>("dart_control"), entry, 0,
        stack, sizeof(stack), cfg::system::thread_priority, cfg::system::thread_priority,
        TX_NO_TIME_SLICE, TX_AUTO_START) == TX_SUCCESS ? types::status::ok : types::status::error;
}
}
