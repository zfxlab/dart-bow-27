#include "start.hpp"
#include "messages.hpp"
#include "control.hpp"
#include "launcher.hpp"
#include "dart_motor.hpp"
#include "sensor.hpp"
#include "host.hpp"
#include "vision.hpp"
#include "monitor.hpp"

namespace dart
{
volatile startup_watch startup{};
namespace
{
TX_THREAD thread{};
alignas(8) std::uint8_t stack[2048]{};
void entry(ULONG)
{
    // This startup thread runs above application workers until initialization
    // completes. DM enable waits occur in the motor worker after it is started.
    startup.stage = 1;
    startup.status = topics::init();
    if (startup.status != types::status::ok) return;
    startup.stage = 2;
    startup.status = sensor::init();
    if (startup.status != types::status::ok) return;
    startup.stage = 3;
    startup.status = host::init();
    if (startup.status != types::status::ok) return;
    startup.stage = 4;
    startup.status = vision::init();
    if (startup.status != types::status::ok) return;
    startup.stage = 5;
    startup.status = control::init();
    if (startup.status != types::status::ok) return;
    startup.stage = 6;
    startup.status = launcher::init();
    if (startup.status != types::status::ok) return;
    startup.stage = 7;
    startup.status = motor::init();
    if (startup.status != types::status::ok) return;
    startup.stage = 8;
    if constexpr (::config::feature::has_led) startup.status = monitor::init();
    if (startup.status == types::status::ok) startup.stage = 9;
}
}
types::status start() noexcept
{
    const auto result = tx_thread_create(&thread, const_cast<CHAR*>("dart_start"), entry, 0,
        stack, sizeof(stack), 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
    startup.status = result == TX_SUCCESS ? types::status::ok : types::status::error;
    return startup.status;
}
}
