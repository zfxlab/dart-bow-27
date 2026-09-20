#pragma once
#include "usertypes.hpp"
#include <cstdint>

namespace dart
{
struct startup_watch
{
    std::uint32_t stage{};
    types::status status = types::status::not_configured;
};
extern volatile startup_watch startup;
types::status start() noexcept;
}
