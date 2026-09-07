#pragma once
// Generated from board/board.ioc and configs/params.json. Do not edit.

#include "config.hpp"

#if HAS_PS2_DEVICE
#include "ps2.hpp"

namespace remoter::binding {

using ps2_transport = ps2_gpio;

inline ps2& ps2_instance()
{
    static ps2_transport transport{board::device::ps2::cmd, board::device::ps2::data,
                                   board::device::ps2::clk, board::device::ps2::cs};
    static ps2 controller{transport};
    return controller;
}
} // namespace remoter::binding
#endif // HAS_PS2_DEVICE
