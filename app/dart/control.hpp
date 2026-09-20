#pragma once
#include "configuration.hpp"
#include "remoter.hpp"

namespace dart::control
{
struct watch
{
    command cmd{};
    remoter::state rc{};
    vision_rx vision{};
    referee_data referee{};
    bool auto_control{}, game_gate_closed{};
    std::uint32_t loops{};
};
extern watch debug;
void map_remote(const remoter::state& rc, command& cmd) noexcept;
void auto_command(const vision_rx& vision, command& cmd) noexcept;
types::status init() noexcept;
}
