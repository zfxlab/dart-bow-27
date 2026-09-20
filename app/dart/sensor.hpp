#pragma once
#include "messages.hpp"

namespace dart::sensor
{
struct watch
{
    sensor_data fdb{};
    std::uint32_t raw_l{}, raw_r{}, frames{}, restarts{}, loops{};
    types::status uart_status = types::status::not_configured;
    types::status gpio_status = types::status::not_configured;
};
extern watch debug;
types::status init() noexcept;
}
