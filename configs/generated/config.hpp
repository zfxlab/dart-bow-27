#pragma once
// Generated from board/board.ioc + configs/params.json + configs/robot.json. Do not edit.

#include <array>
#include <cstddef>
#include <cstdint>

#define HW_HAS_USB 1
#define ENABLE_USBX 1
#define HAS_AHRS 1
#define HAS_BMI088_HEATER 1
#define HAS_DMIMU 1
#define HAS_REMOTER 1
#define HAS_VT03 1
#define HAS_PS2_DEVICE 1
#define PS2_BACKEND_SPI 0
#define PS2_BACKEND_GPIO 1
#define HAS_PS2_UART 1
#define ENABLE_DR16 0
#define ENABLE_VT03 0
#define ENABLE_PS2 1
#define ENABLE_PS2_UART 0
#define HAS_REFEREE 1
#define HAS_UI 1
#define HAS_LED 1
#define HAS_PWM_TIM12_CH2 1
#define HAS_PWM_TIM3_CH4 1
#define HAS_MOTORS 1
#define CAN_DIAG_ENABLED 1
#define MOTOR_DJI 1
#define MOTOR_DM 1
#define MOTOR_LK 0
#define MOTOR_XV2 0

namespace config::feature {

inline constexpr bool hw_has_usb = 1;
inline constexpr bool enable_usbx = 1;
inline constexpr bool has_ahrs = 1;
inline constexpr bool has_bmi088_heater = 1;
inline constexpr bool has_dmimu = 1;
inline constexpr bool has_remoter = 1;
inline constexpr bool has_vt03 = 1;
inline constexpr bool has_ps2_device = 1;
inline constexpr bool has_ps2_uart = 1;
inline constexpr bool enable_dr16 = 0;
inline constexpr bool enable_vt03 = 0;
inline constexpr bool enable_ps2 = 1;
inline constexpr bool enable_ps2_uart = 0;
inline constexpr bool has_referee = 1;
inline constexpr bool has_ui = 1;
inline constexpr bool has_led = 1;
inline constexpr bool has_pwm_tim12_ch2 = true;
inline constexpr bool has_pwm_tim3_ch4 = true;
inline constexpr bool has_motors = 1;
inline constexpr bool motor_dji = 1;
inline constexpr bool motor_dm = 1;
inline constexpr bool motor_lk = 0;
inline constexpr bool motor_xv2 = 0;

inline constexpr bool can_diag = 1;

} // namespace config::feature

namespace bsp {
namespace can {

enum class bus_type : std::uint8_t { classic = 0, fd = 1 };
enum class bus_capability : std::uint8_t { classic = 0, fd_no_brs = 1, fd_brs = 2 };
enum class id_type : std::uint8_t { standard = 0, extended = 1 };
enum class handle_id : std::uint8_t { none = 0, fdcan1, fdcan2, fdcan3 };
enum class bus : std::uint8_t { fdcan1 = 0, fdcan2 = 1, fdcan3 = 2 };

struct bus_config
{
    bool enabled = false;
    handle_id handle = handle_id::none;
    bus_type type = bus_type::classic;
    bus_capability capability = bus_capability::classic;
    id_type filter_id_type = id_type::standard;
};

inline constexpr std::size_t bus_count = 3;
inline constexpr std::size_t max_rx_callbacks = 8;
inline constexpr std::array<bus_config, bus_count> configs = {{ { true, handle_id::fdcan1, bus_type::fd, bus_capability::fd_brs, id_type::standard }, { true, handle_id::fdcan2, bus_type::classic, bus_capability::classic, id_type::standard }, { true, handle_id::fdcan3, bus_type::classic, bus_capability::classic, id_type::standard } }};
inline constexpr std::array<bool, bus_count> enabled = { true, true, true };
inline constexpr std::array<bus_type, bus_count> configured_bus_types = { bus_type::fd, bus_type::classic, bus_type::classic };
inline constexpr std::array<id_type, bus_count> filter_id_types = { id_type::standard, id_type::standard, id_type::standard };

} // namespace can

namespace spi {

enum class handle_id : std::uint8_t { none = 0, spi2, spi6 };
enum class bus : std::uint8_t { spi2 = 0, spi6 = 1 };

struct bus_config
{
    bool enabled = false;
    handle_id handle = handle_id::none;
};

inline constexpr std::size_t bus_count = 2;
inline constexpr std::array<bus_config, bus_count> configs = {{ { true, handle_id::spi2 }, { true, handle_id::spi6 } }};

} // namespace spi

namespace gpio {

enum class port_id : std::uint8_t { none = 0, a, b, c, d, e, f, g, h, i, j, k };
enum class active_level : std::uint8_t { low = 0, high = 1 };
enum class input : std::uint8_t { bmi088_gyro_drdy = 0, ps2_data = 1 };
enum class output : std::uint8_t { bmi088_acc_cs = 0, bmi088_gyro_cs = 1, ps2_cs = 2, ps2_cmd = 3, ps2_clk = 4 };

struct input_config { port_id port; std::uint8_t pin; active_level active; };
struct output_config { port_id port; std::uint8_t pin; active_level active; };

inline constexpr std::size_t input_count = 2;
inline constexpr std::size_t output_count = 5;
inline constexpr std::array<input_config, input_count> input_configs = {{ { port_id::e, 12U, active_level::high }, { port_id::e, 9U, active_level::high } }};
inline constexpr std::array<output_config, output_count> output_configs = {{ { port_id::c, 0U, active_level::low }, { port_id::c, 3U, active_level::low }, { port_id::a, 0U, active_level::low }, { port_id::e, 13U, active_level::high }, { port_id::a, 2U, active_level::high } }};

} // namespace gpio

namespace pwm {

enum class channel : std::uint8_t { tim12_ch2 = 0, tim3_ch4 = 1 };

struct channel_config
{
    std::uint32_t timer_clock_hz = 0;
};

inline constexpr std::size_t channel_count = 2;
inline constexpr std::array<channel_config, channel_count> configs = {{ { 260000000U }, { 260000000U } }};

} // namespace pwm

namespace adc {

enum class channel : std::uint8_t { adc1_ch4 = 0 };
inline constexpr std::size_t channel_count = 1;

} // namespace adc

namespace usart {

using port = std::size_t;

enum class handle_id : std::uint8_t { none = 0, uart5, uart7, usart1, usart10 };

struct port_config
{
    bool enabled = false;
    handle_id handle = handle_id::none;
    bool has_rx_dma = false;
    bool has_tx_dma = false;
};

inline constexpr std::size_t port_count = 4;
inline constexpr std::array<port_config, port_count> configs = {{ { true, handle_id::uart5, true, false }, { true, handle_id::uart7, true, true }, { true, handle_id::usart1, true, true }, { true, handle_id::usart10, true, false } }};
inline constexpr std::array<bool, port_count> enabled = { true, true, true, true };

} // namespace usart
} // namespace bsp

namespace app {
namespace can {

} // namespace can
namespace spi {

} // namespace spi
namespace uart {

inline constexpr bsp::usart::port uart5 = 0;
inline constexpr bsp::usart::port uart7 = 1;
inline constexpr bsp::usart::port usart1 = 2;
inline constexpr bsp::usart::port usart10 = 3;

inline constexpr bsp::usart::port dr16 = uart5;
inline constexpr bsp::usart::port vt03 = uart7;
inline constexpr bsp::usart::port ps2_uart = uart5;
inline constexpr bsp::usart::port referee = usart1;
inline constexpr bsp::usart::port test_report = uart7;

} // namespace uart

namespace gpio {

inline constexpr bsp::gpio::input bmi088_gyro_drdy = bsp::gpio::input::bmi088_gyro_drdy;
inline constexpr bsp::gpio::input ps2_data = bsp::gpio::input::ps2_data;
inline constexpr bsp::gpio::output bmi088_acc_cs = bsp::gpio::output::bmi088_acc_cs;
inline constexpr bsp::gpio::output bmi088_gyro_cs = bsp::gpio::output::bmi088_gyro_cs;
inline constexpr bsp::gpio::output ps2_cs = bsp::gpio::output::ps2_cs;
inline constexpr bsp::gpio::output ps2_cmd = bsp::gpio::output::ps2_cmd;
inline constexpr bsp::gpio::output ps2_clk = bsp::gpio::output::ps2_clk;

} // namespace gpio

namespace pwm {

inline constexpr bsp::pwm::channel bmi088_heater = bsp::pwm::channel::tim3_ch4;

} // namespace pwm

namespace adc {


} // namespace adc
} // namespace app

namespace board::memory {

struct address_range { std::uintptr_t start; std::uintptr_t end; };
inline constexpr bool dma_dedicated_section = 1;
inline constexpr std::size_t cache_line_size = 32U;
inline constexpr std::array<address_range, 2> dma_accessible_ranges = {{ { 0x24000000UL, 0x24050000UL }, { 0x30000000UL, 0x30008000UL } }};

} // namespace board::memory

namespace board::device {

namespace bmi088 {
inline constexpr bsp::spi::bus spi = bsp::spi::bus::spi2;
inline constexpr bsp::gpio::output acc_cs = bsp::gpio::output::bmi088_acc_cs;
inline constexpr bsp::gpio::output gyro_cs = bsp::gpio::output::bmi088_gyro_cs;
inline constexpr bsp::gpio::input gyro_drdy = bsp::gpio::input::bmi088_gyro_drdy;
inline constexpr bsp::pwm::channel heater = bsp::pwm::channel::tim3_ch4;
} // namespace bmi088

namespace led {
inline constexpr bsp::spi::bus spi = bsp::spi::bus::spi6;
} // namespace led
namespace ps2 {
inline constexpr bsp::gpio::output cmd = bsp::gpio::output::ps2_cmd;
inline constexpr bsp::gpio::input data = bsp::gpio::input::ps2_data;
inline constexpr bsp::gpio::output clk = bsp::gpio::output::ps2_clk;
inline constexpr bsp::gpio::output cs = bsp::gpio::output::ps2_cs;
} // namespace ps2
} // namespace board::device

namespace params::ahrs {
  inline constexpr float imu_offset_x = 0.0f;
  inline constexpr std::uint32_t imu_thread_priority = 3;
  inline constexpr std::uint32_t temp_thread_priority = 4;
  inline constexpr float target_temp = 45.0f;
} // namespace params::ahrs

namespace params::dmimu {
enum class communication_mode : std::uint8_t { request = 0, active };
inline constexpr communication_mode mode = communication_mode::active;
inline constexpr std::uint32_t offline_timeout_ticks = 100U;
inline constexpr std::uint32_t thread_priority = 3U;
inline constexpr std::uint32_t receive_wait_ticks = 1U;
inline constexpr std::uint32_t request_period_ticks = 1U;
} // namespace params::dmimu

namespace params::remoter {
  inline constexpr std::uint32_t thread_priority = 2;
  inline constexpr std::uint32_t rx_timeout_ticks = 100;
  inline constexpr std::uint32_t offline_timeout_ticks = 120;
  inline constexpr std::uint32_t ps2_uart_offline_timeout_ticks = 600;
  inline constexpr std::uint32_t ps2_uart_frame_timeout_ticks = 20;
  inline constexpr float ps2_uart_deadzone = 0.08f;
} // namespace params::remoter

namespace params::referee {
  inline constexpr std::uint32_t thread_priority = 8;
} // namespace params::referee

namespace params::test {
  inline constexpr std::uint32_t thread_priority = 10;
  inline constexpr bool auto_run_on_boot = false;
} // namespace params::test

namespace params::usb {
  inline constexpr std::uint32_t read_thread_priority = 5;
  inline constexpr std::uint32_t write_thread_priority = 5;
  inline constexpr std::uint32_t period_ticks = 2;
} // namespace params::usb
namespace params::can_diag {
  inline constexpr std::uint32_t sample_period_ms = 1000;
  inline constexpr std::uint32_t window_size = 60;
} // namespace params::can_diag
