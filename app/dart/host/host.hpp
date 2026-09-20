#pragma once

#include "usertypes.hpp"
#include <cstdint>

namespace dart
{
class dart_state;
struct launcher_status;
namespace host
{
struct debug_state
{
    std::uint32_t rx_bytes{}, rx_frames_ok{}, rx_crc_errors{}, rx_length_errors{};
    std::uint32_t rx_ring_overflow{}, request_full{}, response_full{}, parser_timeouts{};
    std::uint32_t tx_queued{}, tx_busy{}, tx_errors{}, telemetry_dropped{};
    types::status init_status = types::status::not_configured;
};
extern debug_state debug;
// Call once from the application startup thread, after topics::init().
types::status init() noexcept;
// Call only from the system control thread which owns dart_state.
void poll(dart_state& state, const launcher_status& launcher) noexcept;
} // namespace host
} // namespace dart
