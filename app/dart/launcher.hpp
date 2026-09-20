#pragma once
#include "messages.hpp"

namespace dart::launcher
{
// Values remain compatible with oldframe and HostComm.
enum class state : std::uint8_t
{ idle, preparing, ready, firing, hand, error_stop, pre_tension };
enum class prepare : std::uint8_t
{ retract, syn_1, gantry_1, syn_2, gantry_2, trigger_ready, tension };

struct watch
{
    state fsm = state::idle;
    prepare prep = prepare::retract;
    slot current_slot = slot::none;
    bool force_error{}, torque_error{}, gantry_missing{};
    bool left_ready{}, right_ready{}, syn_ready{};
    std::uint32_t loops{};
    command cmd{};
    motor_cmd ref{};
    motor_fdb fdb{};
    sensor_data sensor{};
};
extern watch debug;
float gantry_pos(slot selected) noexcept;
void step(const command& cmd, const sensor_data& sensor, const motor_fdb& fdb,
          motor_cmd& ref, launcher_status& status) noexcept;
types::status init() noexcept;
}
