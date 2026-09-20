#pragma once

#include "msg.hpp"
#include <cstdint>

namespace dart
{
enum class action : std::uint8_t
{
    relax, prepare, syn_adjust, string_adjust, fire, trigger_open,
    trigger_close, yaw_adjust, pre_tension
};
enum class slot : std::uint8_t { none, slot_1, slot_2, slot_3 };
enum class mode : std::uint8_t { speed, position, torque };

// oldframe 7ad4f98 wire format. Do not add fields to these packed packets.
struct vision_rx
{
    std::uint8_t header{}, light_detected{}, stable_state{};
    float yaw{}, distance{};
    std::uint16_t checksum{};
} __attribute__((packed));
struct vision_tx
{
    std::uint8_t header{}, target_id{}, dart_number{};
    float offset{}, yaw{};
    std::uint16_t checksum{};
} __attribute__((packed));
static_assert(sizeof(vision_rx) == 13);
static_assert(sizeof(vision_tx) == 13);

struct command
{
    dart::action action = action::relax;
    slot next_dart_slot = slot::none;
    std::uint8_t current_shot_number{};
    float yaw{}, tension_kg{}, pre_tension_kg{};
    float rc_syn{}, rc_string_l{}, rc_string_r{};
};
struct motor_cmd
{
    float yaw_spd{};
    bool trigger_release{};
    float synbelt_spd{}, synbelt_pos{};
    mode synbelt_mode = mode::position;
    float string_l_spd{}, string_r_spd{};
    float string_l_tension_kg{}, string_r_tension_kg{};
    bool string_able{};
    slot gantry_target_slot = slot::none;
};
struct motor_fdb
{
    float gantry_pos_fdb{}, gantry_spd_fdb{}, gantry_pos_ref{};
    float syn_pos_fdb{}, syn_tq_fdb{}, yaw_pos_fdb{};
    bool gantry_online{}, syn_online{}, yaw_online{};
};
struct sensor_data
{
    bool is_launchplat_return{}, is_trigger_locked{};
    float string_l_force_kg{}, string_r_force_kg{};
    bool force_online{};
};
struct launcher_status
{
    std::uint8_t current_state{}, prepare_state{};
    bool is_fire_finished{}, last_fire_finished{};
};
struct referee_data
{
    std::uint8_t game_status{}, shooting_remaining_time{}, chosen_target{};
    std::uint8_t launch_station_status = 1;
};

namespace topics
{
extern msg::channel<command> cmd;
extern msg::channel<motor_cmd> motor;
extern msg::channel<motor_fdb> fdb;
extern msg::channel<sensor_data> sensor;
extern msg::channel<launcher_status> launcher;
extern msg::channel<vision_rx> vision;
extern msg::channel<vision_tx> vision_out;
extern msg::channel<referee_data> referee;
types::status init() noexcept;
}
} // namespace dart
