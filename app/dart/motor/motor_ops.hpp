#pragma once

#include "constants.hpp"
#include <cstdint>

namespace dart::motor::detail
{
struct can_fragment
{
    std::uint32_t id{};
    std::uint8_t len{}, data[8]{};
};
struct string_frames
{
    can_fragment first{}, last{};
};

// Old X_V2 velocity-with-current-limit command (0xC6), split over Classic CAN.
// This is one application operation, not an alternative generic motor driver.
constexpr string_frames make_string_frames(std::uint8_t address, std::uint8_t positive_dir,
    float speed_rpm, std::uint16_t accel, std::uint16_t current)
{
    const std::uint8_t dir = speed_rpm >= 0.0f ? positive_dir : (positive_dir == 0U ? 1U : 0U);
    const float magnitude = speed_rpm >= 0.0f ? speed_rpm : -speed_rpm;
    const auto speed = static_cast<std::uint16_t>(magnitude * 10.0f);
    return {
        {static_cast<std::uint32_t>(address) << 8U, 8U,
         {0xC6U, dir, static_cast<std::uint8_t>(accel >> 8U), static_cast<std::uint8_t>(accel),
          static_cast<std::uint8_t>(speed >> 8U), static_cast<std::uint8_t>(speed),
          0U, static_cast<std::uint8_t>(current >> 8U)}},
        {(static_cast<std::uint32_t>(address) << 8U) | 1U, 3U,
         {0xC6U, static_cast<std::uint8_t>(current), 0x6BU}}};
}

struct position_state
{
    float last_raw{};
    std::int32_t rounds{};
    bool valid{};
};

// Input is the existing DM driver's wrapped [-pi, pi] feedback. Call only for
// a new feedback sample. A single sample-to-sample move must remain below pi.
constexpr float unfold_position(position_state& state, float raw)
{
    if (state.valid)
    {
        const float delta = raw - state.last_raw;
        if (delta > math::pi) --state.rounds;
        else if (delta < -math::pi) ++state.rounds;
    }
    state.last_raw = raw;
    state.valid = true;
    return raw + static_cast<float>(state.rounds) * 2.0f * math::pi;
}
} // namespace dart::motor::detail
