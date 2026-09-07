#include "remoter_demo.hpp"

#include "config.hpp"
#include "demo_debug.hpp"
#include "msg.hpp"
#include "remoter.hpp"
#include "tx_api.h"

#include <cstdint>
#include <cstring>

namespace diagnose::remoter
{
namespace
{

enum stage : std::uint32_t
{
    service_initialized = 1U << 0U,
    subscriber_created = 1U << 1U,
    monitor_thread_started = 1U << 2U,
    data_received = 1U << 3U,
    remoter_online = 1U << 4U,
    ps2_raw_subscriber_created = 1U << 5U,
    ps2_raw_data_received = 1U << 6U,
};

enum failure : std::uint32_t
{
    service_init_failed = 1U << 0U,
    subscribe_failed = 1U << 1U,
    thread_create_failed = 1U << 2U,
    online_timeout = 1U << 3U,
    ps2_raw_subscribe_failed = 1U << 4U,
};

constexpr ULONG monitor_period_ticks = 5U;
constexpr ULONG online_timeout_ticks = 500U;

TX_THREAD monitor_thread{};
alignas(8) std::uint8_t monitor_stack[768]{};
bool monitor_started = false;
msg::subscriber remoter_sub{};
msg::subscriber ps2_sub{};
ULONG started_at = 0;
::remoter::ps2_uart_state latest_ps2{};
std::uint32_t ps2_raw_update_count = 0;
bool ps2_mapping_pending = false;
std::uint16_t pending_buttons = 0;
std::uint16_t pending_pressed = 0;
std::uint16_t pending_released = 0;
std::uint32_t pending_event_tick = 0;
std::uint32_t ps2_button_event_count = 0;
std::uint32_t ps2_last_button_latency_ticks = 0;
std::uint32_t ps2_max_button_latency_ticks = 0;
std::uint32_t previous_frame_count = 0;
std::uint32_t previous_frame_tick = 0;
std::uint32_t ps2_frame_period_ticks = 0;
std::uint32_t ps2_max_frame_period_ticks = 0;

// This is an application-owned command. The remoter module only supplies its
// normalized state and does not know this type or any of the mappings below.
struct user_command
{
    float x = 0.0f;
    float y = 0.0f;
    bool shoot = false;
    bool relax = true;
};

void apply_command(const user_command& command) noexcept
{
    auto& state = diagnose::debug::debug_instance.remoter_unit;
    state.command_x = command.x;
    state.command_y = command.y;
    state.command_shoot = command.shoot;
    state.command_relax = command.relax;
    ++state.command_update_count;
}

void map_dr16(const ::remoter::state& input)
{
    user_command command{};
    if (!input.offline && input.active_source == ::remoter::source::dr16)
    {
        command.x = input.right_x;
        command.y = input.right_y;
        command.shoot = input.mouse_left;
        command.relax = input.right_sw == ::remoter::sw_state::low;
    }
    apply_command(command);
}

void map_vt03(const ::remoter::state& input)
{
    user_command command{};
    if (!input.offline && input.active_source == ::remoter::source::vt03)
    {
        command.x = input.right_x;
        command.y = input.right_y;
        command.shoot = input.button;
        command.relax = input.pause;
    }
    apply_command(command);
}

void map_ps2_uart(const ::remoter::state& input)
{
    user_command command{};
    if (!input.offline && input.active_source == ::remoter::source::ps2_uart)
    {
        command.x = input.right_x;
        command.y = input.right_y;
        command.shoot = ::remoter::is_held(input.ps2_uart_buttons, ::remoter::ps2_uart_button::r1);
        command.relax = ::remoter::is_held(input.ps2_uart_buttons, ::remoter::ps2_uart_button::select);
    }
    apply_command(command);
}

void map_ps2(const ::remoter::state& input)
{
    user_command command{};
    if (!input.offline && input.active_source == ::remoter::source::ps2)
    {
        command.x = input.right_x;
        command.y = input.right_y;
        command.shoot = ::remoter::is_held(input.ps2_buttons, ::remoter::ps2_button::r1);
        command.relax = ::remoter::is_held(input.ps2_buttons, ::remoter::ps2_button::select);
    }
    apply_command(command);
}

::remoter::update_callback configured_mapping() noexcept
{
    if constexpr (::config::feature::enable_dr16)
    {
        return ::remoter::update_callback::bind<&map_dr16>();
    }
    else if constexpr (::config::feature::enable_vt03)
    {
        return ::remoter::update_callback::bind<&map_vt03>();
    }
    else if constexpr (::config::feature::enable_ps2)
    {
        return ::remoter::update_callback::bind<&map_ps2>();
    }
    else if constexpr (::config::feature::enable_ps2_uart)
    {
        return ::remoter::update_callback::bind<&map_ps2_uart>();
    }
    return {};
}

std::uint16_t key_bits(const ::remoter::key_state& key) noexcept
{
    std::uint16_t bits = 0;
    std::memcpy(&bits, &key, sizeof(bits));
    return bits;
}

template <typename DebugState>
void sync_ps2_debug(DebugState& state, const ::remoter::state& data,
                    const ::remoter::ps2_uart_state& ps2_data) noexcept
{
    if constexpr (static_cast<bool>(ENABLE_PS2_UART))
    {
        state.ps2_link = static_cast<std::uint32_t>(ps2_data.link);
        state.ps2_buttons = data.ps2_uart_buttons;
        state.ps2_raw_buttons = ps2_data.data.ps2_uart_buttons;
        state.ps2_pressed = data.ps2_uart_pressed;
        state.ps2_released = data.ps2_uart_released;
        state.ps2_pressed_seen_mask |= data.ps2_uart_pressed;
        state.ps2_released_seen_mask |= data.ps2_uart_released;
        state.ps2_mapping_match =
            !data.offline && data.active_source == ::remoter::source::ps2_uart &&
            ps2_data.link == ::remoter::ps2_uart_link_state::connected &&
            data.ps2_uart_buttons == ps2_data.data.ps2_uart_buttons;
        state.ps2_mapping_pending = ps2_mapping_pending;
        state.ps2_square = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::square);
        state.ps2_cross = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::cross);
        state.ps2_circle = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::circle);
        state.ps2_triangle = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::triangle);
        state.ps2_r1 = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::r1);
        state.ps2_l1 = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::l1);
        state.ps2_r2 = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::r2);
        state.ps2_l2 = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::l2);
        state.ps2_left = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::left);
        state.ps2_down = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::down);
        state.ps2_right = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::right);
        state.ps2_up = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::up);
        state.ps2_start = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::start);
        state.ps2_r3 = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::r3);
        state.ps2_l3 = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::l3);
        state.ps2_select = ::remoter::is_held(data.ps2_uart_buttons, ::remoter::ps2_uart_button::select);
        state.ps2_raw_left_x = ps2_data.raw_left_x;
        state.ps2_raw_left_y = ps2_data.raw_left_y;
        state.ps2_raw_right_x = ps2_data.raw_right_x;
        state.ps2_raw_right_y = ps2_data.raw_right_y;
        state.ps2_frame_count = ps2_data.frame_count;
        state.ps2_signal_count = ps2_data.signal_count;
        state.ps2_last_signal_tick = ps2_data.last_signal_tick;
        state.ps2_raw_update_count = ps2_raw_update_count;
        state.ps2_upper_event_count = ps2_data.button_event_count;
        state.ps2_button_event_count = ps2_button_event_count;
        state.ps2_last_button_latency_ticks = ps2_last_button_latency_ticks;
        state.ps2_max_button_latency_ticks = ps2_max_button_latency_ticks;
        state.ps2_frame_period_ticks = ps2_frame_period_ticks;
        state.ps2_max_frame_period_ticks = ps2_max_frame_period_ticks;
    }
}

void sync_debug(const ::remoter::state& data, const ::remoter::ps2_uart_state& ps2_data,
                std::uint32_t stages, bool timed_out) noexcept
{
    auto& state = diagnose::debug::debug_instance.remoter_unit;
    if (!data.offline)
    {
        stages |= remoter_online;
    }
    if (data.update_count > 0U)
    {
        stages |= data_received;
    }

    state.stage_mask = stages;
    state.last_step = stages;
    state.offline = data.offline;
    state.source = static_cast<std::uint32_t>(data.active_source);
    sync_ps2_debug(state, data, ps2_data);
    state.left_sw = static_cast<std::uint32_t>(data.left_sw);
    state.right_sw = static_cast<std::uint32_t>(data.right_sw);
    state.last_left_sw = static_cast<std::uint32_t>(data.last_left_sw);
    state.last_right_sw = static_cast<std::uint32_t>(data.last_right_sw);
    state.key_bits = key_bits(data.key);
    state.last_key_bits = key_bits(data.last_key);
    state.update_count = data.update_count;
    state.right_x = data.right_x;
    state.right_y = data.right_y;
    state.left_x = data.left_x;
    state.left_y = data.left_y;
    state.mouse_x = data.mouse_x;
    state.mouse_y = data.mouse_y;

    state.failure_mask = 0;
    if ((stages & service_initialized) == 0U)
    {
        state.failure_mask |= service_init_failed;
    }
    if ((stages & subscriber_created) == 0U)
    {
        state.failure_mask |= subscribe_failed;
    }
    if ((stages & monitor_thread_started) == 0U)
    {
        state.failure_mask |= thread_create_failed;
    }
    if ((stages & ps2_raw_subscriber_created) == 0U)
    {
        state.failure_mask |= ps2_raw_subscribe_failed;
    }
    if (timed_out && data.offline)
    {
        state.failure_mask |= online_timeout;
    }

    state.failed_count = state.failure_mask == 0U ? 0U : 1U;
    state.passed = state.failure_mask == 0U && !data.offline;
    state.passed_count = state.passed ? state.total_count : 0U;
}

void monitor_entry(ULONG /*arg*/)
{
    ::remoter::state data{};
    std::uint32_t stages = service_initialized | subscriber_created |
                           ps2_raw_subscriber_created | monitor_thread_started;
    for (;;)
    {
        ::remoter::ps2_uart_state ps2_data{};
        if (msg::read(ps2_sub, ps2_data) == types::status::ok)
        {
            if (ps2_data.frame_count != previous_frame_count)
            {
                const std::uint32_t frame_delta = ps2_data.frame_count - previous_frame_count;
                if (previous_frame_tick != 0U && frame_delta != 0U)
                {
                    ps2_frame_period_ticks =
                        (ps2_data.last_signal_tick - previous_frame_tick) / frame_delta;
                    if (ps2_frame_period_ticks > ps2_max_frame_period_ticks)
                    {
                        ps2_max_frame_period_ticks = ps2_frame_period_ticks;
                    }
                }
                previous_frame_count = ps2_data.frame_count;
                previous_frame_tick = ps2_data.last_signal_tick;
            }

            if (!ps2_data.data.offline &&
                (ps2_data.data.ps2_uart_pressed != 0U || ps2_data.data.ps2_uart_released != 0U))
            {
                pending_buttons = ps2_data.data.ps2_uart_buttons;
                pending_pressed = ps2_data.data.ps2_uart_pressed;
                pending_released = ps2_data.data.ps2_uart_released;
                pending_event_tick = ps2_data.last_signal_tick;
                ps2_mapping_pending = true;
            }

            latest_ps2 = ps2_data;
            ++ps2_raw_update_count;
            stages |= ps2_raw_data_received;
        }

        if (msg::read(remoter_sub, data) == types::status::ok)
        {
            stages |= data_received;
        }

        if (ps2_mapping_pending && data.ps2_uart_buttons == pending_buttons &&
            data.ps2_uart_pressed == pending_pressed && data.ps2_uart_released == pending_released)
        {
            ps2_last_button_latency_ticks =
                static_cast<std::uint32_t>(tx_time_get()) - pending_event_tick;
            if (ps2_last_button_latency_ticks > ps2_max_button_latency_ticks)
            {
                ps2_max_button_latency_ticks = ps2_last_button_latency_ticks;
            }
            ++ps2_button_event_count;
            ps2_mapping_pending = false;
        }

        sync_debug(data, latest_ps2, stages,
                   (tx_time_get() - started_at) > online_timeout_ticks);
        tx_thread_sleep(monitor_period_ticks);
    }
}

} // namespace

void start() noexcept
{
    auto& state = diagnose::debug::debug_instance.remoter_unit;
    state = {};
    state.started = true;
    state.total_count = 6U;
    started_at = tx_time_get();
    latest_ps2 = {};
    ps2_raw_update_count = 0;
    ps2_mapping_pending = false;
    pending_buttons = 0;
    pending_pressed = 0;
    pending_released = 0;
    pending_event_tick = 0;
    ps2_button_event_count = 0;
    ps2_last_button_latency_ticks = 0;
    ps2_max_button_latency_ticks = 0;
    previous_frame_count = 0;
    previous_frame_tick = 0;
    ps2_frame_period_ticks = 0;
    ps2_max_frame_period_ticks = 0;

    ::remoter::config cfg{};
    cfg.on_update_callback = configured_mapping();

    if (::remoter::service::instance().init(cfg) != types::status::ok)
    {
        state.failure_mask = service_init_failed;
        state.failed_count = 1U;
        return;
    }

    remoter_sub = msg::subscribe(::remoter::service::instance().output());
    if (!remoter_sub.valid())
    {
        state.failure_mask = subscribe_failed;
        state.failed_count = 1U;
        return;
    }

    if constexpr (::config::feature::enable_ps2_uart)
    {
        ps2_sub = msg::subscribe(::remoter::ps2_uart::instance().output());
        if (!ps2_sub.valid())
        {
            state.failure_mask = ps2_raw_subscribe_failed;
            state.failed_count = 1U;
            return;
        }
    }

    if (!monitor_started)
    {
        const UINT status = tx_thread_create(&monitor_thread, const_cast<CHAR*>("remoter_demo"),
                                             monitor_entry, 0, monitor_stack,
                                             sizeof(monitor_stack),
                                             params::remoter::thread_priority + 2U,
                                             params::remoter::thread_priority + 2U,
                                             TX_NO_TIME_SLICE,
                                             TX_AUTO_START);
        if (status != TX_SUCCESS)
        {
            state.failure_mask = thread_create_failed;
            state.failed_count = 1U;
            return;
        }
        monitor_started = true;
    }

    sync_debug({}, {}, service_initialized | subscriber_created |
                           ps2_raw_subscriber_created | monitor_thread_started,
               false);
}

} // namespace diagnose::remoter
