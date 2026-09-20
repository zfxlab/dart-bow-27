#include "vision.hpp"
#include "dart_config.hpp"
#include "bsp_usb.hpp"
#include "crc.hpp"
#include <cstring>

namespace dart::vision
{
watch debug{};
namespace
{
std::uint8_t rx_bytes[sizeof(vision_rx)]{};
std::size_t used{};
// CDC is a byte stream: retain fragments and decode all complete packets.
void on_rx(const std::uint8_t* data, std::uint16_t len)
{
    for (std::uint16_t i = 0; i < len; ++i)
    {
        if (used == 0 && data[i] != 0xa5) continue;
        rx_bytes[used++] = data[i];
        if (used != sizeof(rx_bytes)) continue;
        // oldframe did not validate RX CRC. Keep that compatibility by default;
        // enable params.dart.vision.verify_crc after the camera is verified.
        if (rx_bytes[0] == 0xa5 && (!cfg::vision::verify_crc ||
                                  crc::verify_crc16_checksum(rx_bytes, sizeof(rx_bytes))))
        {
            vision_rx packet{};
            std::memcpy(&packet, rx_bytes, sizeof(packet));
            debug.rx = packet;
            ++debug.rx_frames;
            (void)msg::publish(topics::vision, packet);
            used = 0;
        }
        else
        {
            ++debug.crc_errors;
            std::memmove(rx_bytes, rx_bytes + 1, sizeof(rx_bytes) - 1);
            used = sizeof(rx_bytes) - 1;
        }
    }
}
void on_tx(const bsp::usb::tx_result& result)
{
    if (!result.success()) ++debug.tx_errors;
}
}

types::status init() noexcept
{
    bsp::usb::config cfg{};
    cfg.on_rx_callback = bsp::usb::rx_callback::bind<&on_rx>();
    cfg.on_tx_result_callback = core::callback<void(const bsp::usb::tx_result&)>::bind<&on_tx>();
    return bsp::usb::init(cfg);
}

void send(vision_tx packet, log_frame log) noexcept
{
    crc::append_crc16_checksum(reinterpret_cast<std::uint8_t*>(&packet), sizeof(packet));
    crc::append_crc16_checksum(reinterpret_cast<std::uint8_t*>(&log), sizeof(log));
    std::uint8_t bytes[sizeof(packet) + sizeof(log)]{};
    std::memcpy(bytes, &packet, sizeof(packet));
    std::memcpy(bytes + sizeof(packet), &log, sizeof(log));
    debug.tx = packet;
    debug.log = log;
    const auto result = bsp::usb::try_send(bytes, sizeof(bytes));
    debug.tx_status = result;
    if (result == types::status::ok) ++debug.tx_queued;
    else if (result == types::status::busy) ++debug.tx_busy;
}
}
