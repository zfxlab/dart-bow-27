#include "config.hpp"
#include "tx_api.h"
#include "bsp_can.hpp"
#include "demo_debug.hpp"
#include "imu_demo.hpp"
#include "motor_demo.hpp"
#include "ps2_demo.hpp"
#include "gpio_demo.hpp"
#include "referee_ui_demo.hpp"
#include "remoter_demo.hpp"
#include "usart_demo.hpp"
#include "usb_demo.hpp"

namespace {
void on_can(bsp::can::bus, const bsp::can::rx_frame& frame)
{
    auto& state = diagnose::debug::debug_instance.can;
    ++state.rx_count;
    state.last_seq = frame.id;
    state.last_counter = frame.len;
}
[[maybe_unused]] void start_can(bsp::can::bus bus)
{
    auto& state = diagnose::debug::debug_instance.can;
    if (state.started) return;
    state.started = true;
    auto status = bsp::can::register_rx_callback(bus,
        bsp::can::rx_callback::bind<&on_can>());
    if (status == types::status::ok) status = bsp::can::init(bus);
    state.last_status = static_cast<std::uint8_t>(status);
    state.ready = status == types::status::ok;
    if (!state.ready) ++state.error_count;
}
}

extern "C" void diagnose_start()
{
    if constexpr (params::test::imu && (HAS_AHRS || HAS_DMIMU)) diagnose::imu::start();
    if constexpr (ENABLE_MOTOR_TEST) diagnose::motor::start();
    if constexpr (params::test::remoter && ENABLE_PS2) diagnose::ps2::start();
    if constexpr (ENABLE_GPIO_TEST) diagnose::gpio::start();
    if constexpr (params::test::remoter && !ENABLE_PS2 &&
                  (ENABLE_DR16 || ENABLE_VT03 || ENABLE_PS2_UART)) diagnose::remoter::start();
    if constexpr (params::test::referee_ui && HAS_REFEREE) diagnose::referee_ui::start();
    if constexpr (params::test::usart) diagnose::usart::start();
    if constexpr (params::test::usb && ENABLE_USBX) diagnose::usb::start();
#if ENABLE_CAN_TEST
    start_can(app::can::test_can);
#endif
}

