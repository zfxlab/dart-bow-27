#include "sensor.hpp"
#include "dart_config.hpp"
#include "bsp_usart.hpp"
#include "bsp_gpio.hpp"
#include "memory.h"
#include <cstring>

namespace dart::sensor
{
watch debug{};
namespace
{
TX_THREAD thread{};
alignas(8) std::uint8_t stack[1536]{};
bsp::dma::buffer<64> rx_buffer BSP_DMA_BUFFER{};
std::uint8_t frame[8]{};
std::size_t used{};
volatile std::uint32_t raw_l{}, raw_r{}, frames{};

std::uint32_t u24(const std::uint8_t* p)
{
    return p[0] | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16);
}

// UART IRQ: only assemble the fixed G4 frame and copy the two raw values.
void on_rx(bsp::usart::port, const bsp::usart::rx_frame& rx)
{
    for (std::size_t i = 0; i < rx.len; ++i)
    {
        if (used == 0 && rx.data[i] != 'L') continue;
        frame[used++] = rx.data[i];
        if (used != sizeof(frame)) continue;
        if (frame[0] == 'L' && frame[4] == 'R')
        {
            raw_l = u24(frame + 1);
            raw_r = u24(frame + 5);
            ++frames;
            used = 0;
        }
        else
        {
            std::memmove(frame, frame + 1, sizeof(frame) - 1);
            used = sizeof(frame) - 1;
        }
    }
}

void entry(ULONG)
{
    sensor_data fdb{};
    ULONG last_frame_tick = tx_time_get();
    ULONG last_restart_tick = last_frame_tick;
    std::uint32_t last_frames{};
    for (;;)
    {
        const UINT irq = tx_interrupt_control(TX_INT_DISABLE);
        const std::uint32_t l = raw_l, r = raw_r, count = frames;
        tx_interrupt_control(irq);
        const ULONG now = tx_time_get();
        if (count != last_frames)
        {
            last_frame_tick = now;
            last_frames = count;
        }
        fdb.force_online = count != 0 && now - last_frame_tick < cfg::sensor::timeout_ticks;
        // Keep old measured value on timeout; make its age visible in watch.
        fdb.string_l_force_kg = static_cast<float>(l) / cfg::sensor::force_scale;
        fdb.string_r_force_kg = static_cast<float>(r) / cfg::sensor::force_scale;
        if (!fdb.force_online && now - last_restart_tick >= cfg::sensor::timeout_ticks)
        {
            debug.uart_status = bsp::usart::restart_rx(app::uart::force_sensor);
            last_restart_tick = now;
            ++debug.restarts;
        }
        debug.gpio_status = bsp::gpio::is_active(app::gpio::trigger_locked, fdb.is_trigger_locked);
        const auto platform_status = bsp::gpio::is_active(app::gpio::launch_return, fdb.is_launchplat_return);
        if (platform_status != types::status::ok) debug.gpio_status = platform_status;
        debug.raw_l = l;
        debug.raw_r = r;
        debug.frames = count;
        debug.fdb = fdb;
        ++debug.loops;
        (void)msg::publish(topics::sensor, fdb);
        tx_thread_sleep(cfg::sensor::period_ticks);
    }
}
}

types::status init() noexcept
{
    debug.uart_status = bsp::usart::init(app::uart::force_sensor, bsp::usart::mode::dma);
    if (debug.uart_status != types::status::ok) return debug.uart_status;
    debug.uart_status = bsp::usart::start_rx_to_idle(app::uart::force_sensor, rx_buffer.view(),
                         bsp::usart::rx_callback::bind<&on_rx>());
    if (debug.uart_status != types::status::ok) return debug.uart_status;
    return tx_thread_create(&thread, const_cast<CHAR*>("dart_sensor"), entry, 0,
        stack, sizeof(stack), cfg::sensor::thread_priority, cfg::sensor::thread_priority,
        TX_NO_TIME_SLICE, TX_AUTO_START) == TX_SUCCESS ? types::status::ok : types::status::error;
}
}
