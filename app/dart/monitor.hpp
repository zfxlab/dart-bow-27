#pragma once
#include "usertypes.hpp"
#include <cstdint>

namespace dart::monitor
{
struct watch
{
    bool can_error{}, referee_alive{}, vision_error{}, gantry_error{};
    std::uint8_t pattern{}, phase{}, displayed_color = 0xff;
    std::uint32_t loops{}, led_updates{};
};
extern watch debug;
types::status init() noexcept;
}
