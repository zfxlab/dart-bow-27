#include "ps2_demo.hpp"

#include "bsp_bindings.hpp"
#include "config.hpp"
#include "demo_debug.hpp"
#include "msg.hpp"
#include "ps2.hpp"
#include "tx_api.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace diagnose::ps2
{
namespace
{

enum stage : std::uint32_t
{
    protocol_test_passed = 1U << 0U,
    source_initialized = 1U << 1U,
    subscriber_created = 1U << 2U,
    monitor_started = 1U << 3U,
    valid_frame_received = 1U << 4U,
};

enum failure : std::uint32_t
{
    protocol_test_failed = 1U << 0U,
    source_init_failed = 1U << 1U,
    subscribe_failed = 1U << 2U,
    thread_create_failed = 1U << 3U,
    analog_frame_timeout = 1U << 4U,
};

constexpr std::uint32_t pass_stage_mask = protocol_test_passed | source_initialized |
                                          subscriber_created | monitor_started |
                                          valid_frame_received;
constexpr ULONG monitor_period_ticks = 5U;
constexpr ULONG analog_frame_timeout_ticks = 1000U;

class fake_transport
{
public:
    remoter::ps2_raw_frame response{};
    std::array<std::uint8_t, remoter::ps2_protocol::frame_size> query{};

    types::status transfer(const std::uint8_t* tx, std::uint8_t* rx, std::size_t size)
    {
        if (tx == nullptr || rx == nullptr || size != response.bytes.size())
        {
            return types::status::invalid_arg;
        }
        for (std::size_t index = 0U; index < size; ++index)
        {
            query[index] = tx[index];
            rx[index] = response.bytes[index];
        }
        return types::status::ok;
    }
};

constexpr std::array<std::uint8_t, remoter::ps2_protocol::frame_size> poll_query{
    0x01U, 0x42U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};

TX_THREAD monitor_thread{};
alignas(8) std::uint8_t monitor_stack[768]{};
msg::subscriber ps2_sub{};
bool started = false;
ULONG started_at = 0U;
std::uint32_t stages = 0U;

bool run_protocol_test() noexcept
{
    fake_transport transport{};
    transport.response.bytes = {0xFFU, 0x73U, 0x5AU, 0xF7U, 0xBFU,
                                0x00U, 0x00U, 0xFFU, 0xFFU};
    remoter::ps2_controller_state state{};
    remoter::ps2_raw_frame raw{};
    if (remoter::ps2_protocol::poll(transport, state, &raw) != types::status::ok ||
        transport.query != poll_query || raw.bytes != transport.response.bytes || !state.start ||
        !state.cross || state.select || state.circle || state.rx != -1.0f || state.ry != 1.0f ||
        state.lx != 1.0f || state.ly != -0.9921875f)
    {
        return false;
    }

    transport.response.bytes = {0xFFU, 0x73U, 0x5AU, 0xFFU, 0xFFU,
                                0x7FU, 0x80U, 0x7FU, 0x80U};
    if (remoter::ps2_protocol::poll(transport, state, &raw) != types::status::ok ||
        state.rx != 0.0f || state.ry != 0.0f || state.lx != 0.0f || state.ly != 0.0f)
    {
        return false;
    }

    transport.response.bytes[1] = 0x41U;
    return remoter::ps2_protocol::poll(transport, state, &raw) == types::status::error;
}

void sync_summary() noexcept
{
    auto& state = debug::debug_instance.ps2_unit;
    state.stage_mask = stages;
    state.last_step = stages;
    state.observed_count = state.valid_frame_count;
    state.failed_count = state.failure_mask == 0U ? 0U : 1U;
    state.passed = (stages & pass_stage_mask) == pass_stage_mask && state.failure_mask == 0U;
    state.passed_count = state.passed ? state.total_count : 0U;
}

void record(const remoter::ps2_state& input) noexcept
{
    auto& state = debug::debug_instance.ps2_unit;
    state.last_status = input.frame_valid ? protocol::status_code(types::status::ok)
                                          : protocol::status_code(types::status::error);
    state.controller_id = input.controller_id;
    for (std::size_t index = 0U; index < input.raw.bytes.size(); ++index)
    {
        state.raw[index] = input.raw.bytes[index];
    }
    state.poll_count = input.poll_count;
    state.valid_frame_count = input.valid_frame_count;
    state.error_count = input.error_count;
    state.frame_valid = input.frame_valid;
    state.analog_mode = input.controller_id == 0x53U || input.controller_id == 0x73U;
    state.select = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::select);
    state.start = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::start);
    state.up = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::up);
    state.down = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::down);
    state.left = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::left);
    state.right = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::right);
    state.l1 = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::l1);
    state.l2 = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::l2);
    state.r1 = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::r1);
    state.r2 = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::r2);
    state.triangle = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::triangle);
    state.circle = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::circle);
    state.cross = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::cross);
    state.square = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::square);
    state.l3 = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::l3);
    state.r3 = remoter::is_held(input.data.ps2_buttons, remoter::ps2_button::r3);
    state.lx = input.data.left_x;
    state.ly = input.data.left_y;
    state.rx = input.data.right_x;
    state.ry = input.data.right_y;
    if (input.frame_valid)
    {
        stages |= valid_frame_received;
    }
    else if (input.valid_frame_count == 0U && tx_time_get() - started_at >= analog_frame_timeout_ticks)
    {
        state.failure_mask |= analog_frame_timeout;
    }
    sync_summary();
}

#if ENABLE_PS2
void monitor_entry(ULONG)
{
    for (;;)
    {
        remoter::ps2_state input{};
        if (msg::read(ps2_sub, input) == types::status::ok)
        {
            record(input);
        }
        tx_thread_sleep(monitor_period_ticks);
    }
}
#endif

} // namespace

void start() noexcept
{
    if (started)
    {
        return;
    }
    auto& state = debug::debug_instance.ps2_unit;
    state = {};
    state.started = true;
    state.total_count = 5U;
    state.compiled_enabled = ENABLE_PS2 != 0;
    started_at = tx_time_get();
    stages = 0U;
    if (run_protocol_test())
    {
        stages |= protocol_test_passed;
    }
    else
    {
        state.failure_mask |= protocol_test_failed;
    }

#if ENABLE_PS2
    const types::status init_status = remoter::binding::ps2_instance().init();
    ++state.init_count;
    state.initialized = init_status == types::status::ok;
    state.last_status = protocol::status_code(init_status);
    if (init_status != types::status::ok)
    {
        state.failure_mask |= source_init_failed;
        sync_summary();
        return;
    }
    stages |= source_initialized;
    ps2_sub = msg::subscribe(remoter::binding::ps2_instance().output());
    if (!ps2_sub.valid())
    {
        state.failure_mask |= subscribe_failed;
        sync_summary();
        return;
    }
    stages |= subscriber_created;
    if (tx_thread_create(&monitor_thread, const_cast<CHAR*>("ps2_diagnose"), monitor_entry, 0U,
                         monitor_stack, sizeof(monitor_stack), 10U, 10U, TX_NO_TIME_SLICE,
                         TX_AUTO_START) != TX_SUCCESS)
    {
        state.failure_mask |= thread_create_failed;
        sync_summary();
        return;
    }
    stages |= monitor_started;
    started = true;
#else
    state.failure_mask |= source_init_failed;
#endif
    sync_summary();
}

} // namespace diagnose::ps2
