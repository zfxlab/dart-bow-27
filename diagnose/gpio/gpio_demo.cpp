#include "gpio_demo.hpp"

#include "bsp_gpio.hpp"
#include "config.hpp"
#include "demo_debug.hpp"
#include "tx_api.h"

#include <cstdint>

namespace diagnose::gpio
{
namespace
{

static_assert(ENABLE_GPIO_TEST, "gpio_demo.cpp is selected only for configured GPIO test roles");

constexpr auto test_output = app::gpio::test_gpio;

TX_THREAD test_thread{};
alignas(8) std::uint8_t test_stack[512]{};
bool started = false;

void test_thread_entry(ULONG) noexcept
{
    auto& state = debug::debug_instance.gpio_unit;
    bool active = true;
    constexpr ULONG dwell_ticks = TX_TIMER_TICKS_PER_SECOND / 2U;

    while (true)
    {
        if (bsp::gpio::set_active(test_output, active) == types::status::ok)
        {
            state.last_step = static_cast<std::uint32_t>(active);
            ++state.observed_count;
        }
        else
        {
            state.failure_mask |= 1U;
            ++state.failed_count;
            state.passed = false;
        }
        active = !active;
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
    state.total_count = 1U;
    if (!bsp::gpio::is_enabled(test_output) ||
        bsp::gpio::set_active(test_output, false) != types::status::ok)
    {
        state.failure_mask |= 1U;
        ++state.failed_count;
        return types::status::not_configured;
    }
    ++state.passed_count;
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
