#include "monitor.hpp"
#include "dart_config.hpp"
#include "messages.hpp"
#include "robot_config.hpp"
#include "led.hpp"
#include "referee.hpp"
#include "bsp_can.hpp"

namespace dart::monitor
{
watch debug{};
namespace
{
TX_THREAD thread{};
alignas(8) std::uint8_t stack[1024]{};
msg::subscriber vision_sub{}, fdb_sub{};

bool take(TX_SEMAPHORE* sem)
{
    return sem != nullptr && tx_semaphore_get(sem, TX_NO_WAIT) == TX_SUCCESS;
}

bool read_can_error()
{
    const bsp::can::bus buses[] = {
        robot::motors::synbelt.can_bus, robot::motors::yaw.can_bus,
        robot::motors::string_l.can_bus, robot::motors::string_r.can_bus};
    bool error = false;
    for (unsigned i = 0; i < 4; ++i)
    {
        bool duplicate = false;
        for (unsigned j = 0; j < i; ++j) duplicate |= buses[i] == buses[j];
        // Motor workers initialize buses after app startup. Resolve the public
        // semaphore here so a not-yet-created semaphore is not cached forever.
        if (!duplicate) error |= take(bsp::can::err_sem(buses[i]));
    }
    return error;
}

void display(int color)
{
    // 0xff denotes off. The LED API sends a blocking SPI burst for each call.
    const auto value = static_cast<std::uint8_t>(color);
    if (debug.displayed_color == value) return;
    if (color < 0) led::all_off();
    else led::on(static_cast<led::color>(color));
    debug.displayed_color = value;
    ++debug.led_updates;
}

void entry(ULONG)
{
    vision_rx vision{};
    motor_fdb fdb{};
    // The old single/two/three-color blink helpers each kept their own phase.
    ULONG phase_ticks[4]{};
    ULONG last_tick = tx_time_get();
    led::init();
    debug.displayed_color = static_cast<std::uint8_t>(led::color::white);
    ++debug.led_updates;
    for (;;)
    {
        (void)msg::read(vision_sub, vision);
        (void)msg::read(fdb_sub, fdb);
        debug.can_error = read_can_error();
        debug.referee_alive = take(referee::service::instance().heartbeat_sem());
        debug.vision_error = vision.header != 0xa5 || vision.distance == 0.0f || vision.checksum == 0;
        // oldframe left gantry unregistered, so retain the white indication.
        debug.gantry_error = !fdb.gantry_online;
        led::color colors[3]{};
        unsigned count = 0;
        if (debug.can_error) colors[count++] = led::color::red;
        if (debug.vision_error) colors[count++] = led::color::blue;
        if (debug.gantry_error) colors[count++] = led::color::white;
        if (count == 0 && debug.referee_alive) colors[count++] = led::color::green;
        const std::uint8_t pattern = (debug.can_error ? 1 : 0) |
            (debug.vision_error ? 2 : 0) | (debug.gantry_error ? 4 : 0) |
            (count == 1 && colors[0] == led::color::green ? 8 : 0);
        const ULONG now = tx_time_get();
        const unsigned phases = count == 1 ? 2 : count;
        if (count != 0)
            phase_ticks[count] = (phase_ticks[count] + now - last_tick) %
                (cfg::monitor::phase_ticks * phases);
        last_tick = now;
        const unsigned phase = count == 0 ? 0 :
            static_cast<unsigned>(phase_ticks[count] / cfg::monitor::phase_ticks);
        int color = -1;
        if (count > 1) color = static_cast<int>(colors[phase]);
        else if (count == 1 && phase == 1) color = static_cast<int>(colors[0]);
        display(color);
        debug.pattern = pattern;
        debug.phase = static_cast<std::uint8_t>(phase);
        ++debug.loops;
        tx_thread_sleep(cfg::monitor::period_ticks);
    }
}
}

types::status init() noexcept
{
    vision_sub = msg::subscribe(topics::vision);
    fdb_sub = msg::subscribe(topics::fdb);
    if (!vision_sub.valid() || !fdb_sub.valid()) return types::status::error;
    return tx_thread_create(&thread, const_cast<CHAR*>("dart_monitor"), entry, 0,
        stack, sizeof(stack), cfg::monitor::thread_priority, cfg::monitor::thread_priority,
        TX_NO_TIME_SLICE, TX_AUTO_START) == TX_SUCCESS ? types::status::ok : types::status::error;
}
}
