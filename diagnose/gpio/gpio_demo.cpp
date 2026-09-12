#include "gpio_demo.hpp"

#include "bsp_gpio.hpp"
#include "config.hpp"
#include "demo_debug.hpp"
#include "tx_api.h"

#include <array>
#include <cstdint>

namespace diagnose::gpio
{
namespace
{

static_assert(ENABLE_GPIO_TEST, "gpio_demo.cpp is selected only for configured GPIO test roles");

constexpr std::array<bsp::gpio::output, 3U> leds{
    app::gpio::led_r,
    app::gpio::led_g,
    app::gpio::led_b,
};

TX_THREAD test_thread{};
alignas(8) std::uint8_t test_stack[512]{};
bool started = false;

bool set_only_active(std::size_t active_index) noexcept
{
    for (std::size_t index = 0U; index < leds.size(); ++index)
    {
        if (bsp::gpio::set_active(leds[index], index == active_index) != types::status::ok)
        {
            return false;
        }
    }
    return true;
}

void test_thread_entry(ULONG) noexcept
{
    auto& state = debug::debug_instance.gpio_unit;
    std::size_t active_index = 0U;
    constexpr ULONG dwell_ticks = TX_TIMER_TICKS_PER_SECOND / 2U;

    while (true)
    {
        if (set_only_active(active_index))
        {
            state.last_step = static_cast<std::uint32_t>(active_index);
            ++state.observed_count;
        }
        else
        {
            state.failure_mask |= 1U << static_cast<std::uint32_t>(active_index);
            ++state.failed_count;
            state.passed = false;
        }
        active_index = (active_index + 1U) % leds.size();
        tx_thread_sleep(dwell_ticks == 0U ? 1U : dwell_ticks);
    }
}

} // namespace

types::status start() noexcept
{
    auto& state = debug::debug_instance.gpio_unit;
    if (started)
    {
        return types::status::ok;
    }

    state = {};
    state.started = true;
    state.total_count = static_cast<std::uint32_t>(leds.size());
    for (std::size_t index = 0U; index < leds.size(); ++index)
    {
        if (!bsp::gpio::is_enabled(leds[index]) ||
            bsp::gpio::set_active(leds[index], false) != types::status::ok)
        {
            state.failure_mask |= 1U << static_cast<std::uint32_t>(index);
            ++state.failed_count;
            return types::status::not_configured;
        }
        ++state.passed_count;
    }
    state.passed = true;

    if (tx_thread_create(&test_thread, const_cast<CHAR*>("gpio_test"), test_thread_entry, 0U,
                         test_stack, sizeof(test_stack), 10U, 10U, TX_NO_TIME_SLICE,
                         TX_AUTO_START) != TX_SUCCESS)
    {
        state.failure_mask |= 1U << 31U;
        ++state.failed_count;
        state.passed = false;
        return types::status::error;
    }

    started = true;
    return types::status::ok;
}

} // namespace diagnose::gpio
