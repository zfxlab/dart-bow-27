#pragma once

#include "usertypes.hpp"

namespace diagnose::gpio
{

// Starts the board-configured RGB GPIO indication test.
types::status start() noexcept;

} // namespace diagnose::gpio
