#pragma once
#include "messages.hpp"

namespace dart::vision
{
struct log_frame
{
    std::uint8_t header = 0xd5;
    std::uint8_t state{}, prepare_state{}, launch_station_status{};
    bool is_fire_finished{};
    std::uint8_t fired_count_this_open{}, current_shot_number{}, current_dart_id{};
    std::uint8_t door_status{}, last_light_detected{}, vision_light_detected{}, vision_stable_state{};
    bool door_session_active{}, autoaim_allow{}, door_close_inhibit_active{};
    float string_l_force_kg{}, string_r_force_kg{};
    std::uint16_t checksum{};
} __attribute__((packed));
static_assert(sizeof(log_frame) == 25);
struct watch
{
    vision_rx rx{};
    vision_tx tx{};
    log_frame log{};
    std::uint32_t rx_frames{}, crc_errors{}, tx_queued{}, tx_busy{}, tx_errors{};
    types::status tx_status = types::status::not_initialized;
};
extern watch debug;
types::status init() noexcept;
void send(vision_tx packet, log_frame log) noexcept;
}
