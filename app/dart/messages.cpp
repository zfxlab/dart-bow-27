#include "messages.hpp"

namespace dart::topics
{
msg::channel<command> cmd{};
msg::channel<motor_cmd> motor{};
msg::channel<motor_fdb> fdb{};
msg::channel<sensor_data> sensor{};
msg::channel<launcher_status> launcher{};
msg::channel<vision_rx> vision{};
msg::channel<vision_tx> vision_out{};
msg::channel<referee_data> referee{};

types::status init() noexcept
{
    const types::status results[] = {
        msg::init(cmd), msg::init(motor), msg::init(fdb), msg::init(sensor),
        msg::init(launcher), msg::init(vision), msg::init(vision_out), msg::init(referee)
    };
    for (auto result : results)
        if (result != types::status::ok) return result;
    return types::status::ok;
}
}
