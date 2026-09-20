#include "host.hpp"
#include "internal.hpp"
#include "configuration.hpp"
#include "messages.hpp"
#include "launcher.hpp"
#include "dart_config.hpp"

#include <cmath>
#include <cstring>

namespace dart::host
{
namespace
{
constexpr float HOST_YAW_MIN = -5.0f;
constexpr float HOST_YAW_MAX = 5.0f;
constexpr float HOST_TENSION_MIN = 0.0f;
constexpr float HOST_TENSION_MAX = 120.0f;
constexpr float HOST_PRE_TENSION_MIN = 0.0f;
constexpr float HOST_PRE_TENSION_MAX = 120.0f;
msg::subscriber sensor_sub{}, motor_sub{};
sensor_data latest_sensor{};
motor_cmd latest_motor{};
ULONG telemetry_tick{};
uint8_t telemetry_seq{};
response pending_batch[17]{};
std::size_t pending_count{};

bool reset_allowed(const launcher_status& status)
{
    return status.current_state == static_cast<uint8_t>(launcher::state::idle) ||
           status.current_state == static_cast<uint8_t>(launcher::state::error_stop);
}

void put_u16(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    out[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

void put_float(uint8_t* out, float value)
{
    memcpy(out, &value, sizeof(value));
}

void queue_reply(const response& tx)
{
    // The largest request is GET_DART_TABLE: 16 records plus one ACK.
    pending_batch[pending_count++] = tx;
}

void send_ack(uint8_t ref_type, uint8_t ref_seq,
                 uint8_t result, uint16_t revision)
{
    response tx{};
    tx.valid = 1;
    tx.type = HOST_TYPE_ACK;
    tx.seq = ref_seq;
    tx.len = 6;
    tx.payload[0] = ref_type;
    tx.payload[1] = ref_seq;
    tx.payload[2] = result;
    tx.payload[3] = 0;
    put_u16(&tx.payload[4], revision);
    queue_reply(tx);
}

void send_error(uint8_t ref_type, uint8_t ref_seq,
                   uint8_t error_code, uint16_t detail)
{
    response tx{};
    tx.valid = 1;
    tx.type = HOST_TYPE_ERROR_RSP;
    tx.seq = ref_seq;
    tx.len = 6;
    tx.payload[0] = ref_type;
    tx.payload[1] = ref_seq;
    tx.payload[2] = error_code;
    tx.payload[3] = 0;
    put_u16(&tx.payload[4], detail);
    queue_reply(tx);
}

void send_dart(const dart_state& dart, uint8_t seq, uint8_t dart_id)
{
    if (dart_id < 1 || dart_id > 16)
    {
        return;
    }

    const dart_param& item = dart.config.dart[dart_id];
    response tx{};
    tx.valid = 1;
    tx.type = HOST_TYPE_DART_PARAM_STATE;
    tx.seq = seq;
    tx.len = 15;
    put_u16(&tx.payload[0], dart.config.config_revision);
    tx.payload[2] = dart_id;
    put_float(&tx.payload[3], item.yaw_offset);
    put_float(&tx.payload[7], item.tension_kg_base);
    put_float(&tx.payload[11], item.tension_kg_outpost);
    queue_reply(tx);
}

void send_sequence(const dart_state& dart, uint8_t seq)
{
    response tx{};
    tx.valid = 1;
    tx.type = HOST_TYPE_SEQUENCE_STATE;
    tx.seq = seq;
    tx.len = 6;
    put_u16(&tx.payload[0], dart.config.config_revision);
    for (uint8_t i = 0; i < 4; i++)
    {
        tx.payload[2 + i] = static_cast<uint8_t>(dart.config.sequence[i]);
    }
    queue_reply(tx);
}

void send_pre_tension(const dart_state& dart, uint8_t seq)
{
    response tx{};
    tx.valid = 1;
    tx.type = HOST_TYPE_PRE_TENSION_STATE;
    tx.seq = seq;
    tx.len = 6;
    put_u16(&tx.payload[0], dart.config.config_revision);
    put_float(&tx.payload[2], dart.config.pre_tension_kg);
    queue_reply(tx);
}

void send_heartbeat(const dart_state& dart, uint8_t seq)
{
    response tx{};
    tx.valid = 1;
    tx.type = HOST_TYPE_HEARTBEAT_RSP;
    tx.seq = seq;
    tx.len = 8;
    tx.payload[0] = 0;
    tx.payload[1] = HOSTCOMM_PROTOCOL_VERSION;
    put_u32(&tx.payload[2], tx_time_get());
    put_u16(&tx.payload[6], dart.config.config_revision);
    queue_reply(tx);
}

void send_telemetry(uint8_t seq,
                            const motor_cmd& motorctrl,
                            const sensor_data& sensor)
{
    response tx{};
    tx.valid = 1;
    tx.type = HOST_TYPE_FAST_TELEMETRY;
    tx.seq = seq;
    tx.len = 21;
    put_u32(&tx.payload[0], tx_time_get());
    put_float(&tx.payload[4], motorctrl.string_l_tension_kg);
    put_float(&tx.payload[8], motorctrl.string_r_tension_kg);
    put_float(&tx.payload[12], sensor.string_l_force_kg);
    put_float(&tx.payload[16], sensor.string_r_force_kg);
    tx.payload[20] = motorctrl.string_able ? 1U : 0U;

    // Telemetry is best effort; command replies retain their FIFO order.
    if (!push_responses(&tx, 1)) { ++debug.telemetry_dropped; }
}

bool in_range(float value, float min_value, float max_value)
{
    return std::isfinite(value) && value >= min_value && value <= max_value;
}
void process_request(const request& req, const launcher_status& lch2sys,
                                     dart_state& dart)
{
    if (!req.valid)
    {
        return;
    }

    switch (req.kind)
    {
        case HOST_REQ_HEARTBEAT:
            send_heartbeat(dart, req.seq);
            break;

        case HOST_REQ_GET_DART_TABLE:
            for (uint8_t id = 1; id <= 16; id++)
            {
                send_dart(dart, req.seq, id);
            }
            send_ack(req.type, req.seq, HOST_ACK_APPLIED, dart.config.config_revision);
            break;

        case HOST_REQ_SET_DART_PARAMS_BATCH:
        {
            if (req.target_type > 1 ||
                req.update_mask == 0 ||
                (req.update_mask & static_cast<uint8_t>(~0x03U)) != 0 ||
                req.dart_count == 0 ||
                req.dart_count > 8)
            {
                send_error(req.type, req.seq, HOST_ERR_INVALID_COMMAND, req.update_mask);
                break;
            }

            if ((req.update_mask & 0x01U) != 0 &&
                !in_range(req.yaw_offset, HOST_YAW_MIN, HOST_YAW_MAX))
            {
                send_error(req.type, req.seq, HOST_ERR_PARAM_OUT_OF_RANGE, 1);
                break;
            }

            if ((req.update_mask & 0x02U) != 0 &&
                !in_range(req.tension, HOST_TENSION_MIN, HOST_TENSION_MAX))
            {
                send_error(req.type, req.seq, HOST_ERR_PARAM_OUT_OF_RANGE, 2);
                break;
            }

            bool seen[17] = {false};
            bool valid_ids = true;
            bool duplicate = false;
            for (uint8_t i = 0; i < req.dart_count; i++)
            {
                const uint8_t id = req.dart_ids[i];
                if (id < 1 || id > 16)
                {
                    valid_ids = false;
                    break;
                }
                if (seen[id])
                {
                    duplicate = true;
                    break;
                }
                seen[id] = true;
            }

            if (!valid_ids)
            {
                send_error(req.type, req.seq, HOST_ERR_PARAM_OUT_OF_RANGE, 3);
                break;
            }
            if (duplicate)
            {
                send_error(req.type, req.seq, HOST_ERR_DUPLICATE_DART_ID, 0);
                break;
            }

            for (uint8_t i = 0; i < req.dart_count; i++)
            {
                dart_param& item = dart.config.dart[req.dart_ids[i]];
                if ((req.update_mask & 0x01U) != 0)
                {
                    item.yaw_offset = req.yaw_offset;
                }
                if ((req.update_mask & 0x02U) != 0)
                {
                    if (req.target_type == 0)
                    {
                        item.tension_kg_base = req.tension;
                    }
                    else
                    {
                        item.tension_kg_outpost = req.tension;
                    }
                }
            }

            dart.config.config_revision++;
            send_ack(req.type, req.seq, HOST_ACK_APPLIED, dart.config.config_revision);
            for (uint8_t i = 0; i < req.dart_count; i++)
            {
                send_dart(dart, req.seq, req.dart_ids[i]);
            }
            break;
        }

        case HOST_REQ_GET_SEQUENCE:
            send_sequence(dart, req.seq);
            break;

        case HOST_REQ_SET_SEQUENCE:
            for (uint8_t i = 0; i < 4; i++)
            {
                if (req.sequence[i] < 1 || req.sequence[i] > 16)
                {
                    send_error(req.type, req.seq, HOST_ERR_PARAM_OUT_OF_RANGE, i);
                    return;
                }
            }
            for (uint8_t i = 0; i < 4; i++)
            {
                dart.config.sequence[i] = req.sequence[i];
            }
            dart.update_dart_id();
            dart.config.config_revision++;
            send_ack(req.type, req.seq, HOST_ACK_APPLIED, dart.config.config_revision);
            send_sequence(dart, req.seq);
            break;

        case HOST_REQ_GET_PRE_TENSION:
            send_pre_tension(dart, req.seq);
            break;

        case HOST_REQ_SET_PRE_TENSION:
            if (!in_range(req.pre_tension, HOST_PRE_TENSION_MIN, HOST_PRE_TENSION_MAX))
            {
                send_error(req.type, req.seq, HOST_ERR_PARAM_OUT_OF_RANGE, 0);
                break;
            }
            dart.config.pre_tension_kg = req.pre_tension;
            dart.config.config_revision++;
            send_ack(req.type, req.seq, HOST_ACK_APPLIED, dart.config.config_revision);
            send_pre_tension(dart, req.seq);
            break;

        case HOST_REQ_RESET_TEST_ROUND:
            if (!reset_allowed(lch2sys))
            {
                send_error(req.type, req.seq, HOST_ERR_STATE_FORBIDDEN, lch2sys.current_state);
                break;
            }

            dart.runtime.current_shot_number = 1;
            dart.update_dart_id();
            dart.runtime.fired_count_this_open = 0;
            dart.runtime.last_fire_finished = lch2sys.is_fire_finished;
            dart.runtime.auto_aim.yaw_ok = false;
            send_ack(req.type, req.seq, HOST_ACK_APPLIED, dart.config.config_revision);
            break;

        default:
            send_error(req.type, req.seq, HOST_ERR_UNKNOWN_TYPE, req.kind);
            break;
    }
}

} // namespace

types::status init_service() noexcept
{
    sensor_sub = msg::subscribe(topics::sensor);
    motor_sub = msg::subscribe(topics::motor);
    return sensor_sub.valid() && motor_sub.valid() ? types::status::ok : types::status::error;
}

void poll(dart_state& dart, const launcher_status& launcher) noexcept
{
    (void)msg::read(sensor_sub, latest_sensor);
    (void)msg::read(motor_sub, latest_motor);
    // If the UART queue fills, retain all replies and resume next control tick.
    if (pending_count != 0)
    {
        if (!push_responses(pending_batch, pending_count)) { return; }
        pending_count = 0;
    }
    request req{};
    for (unsigned i = 0; i < 4 && pop_request(req); ++i)
    {
        process_request(req, launcher, dart);
        if (!push_responses(pending_batch, pending_count)) { return; }
        pending_count = 0;
    }
    const ULONG now = tx_time_get();
    if (now - telemetry_tick >= cfg::host::telemetry_ticks)
    {
        telemetry_tick = now;
        send_telemetry(telemetry_seq++, latest_motor, latest_sensor);
    }
}
} // namespace dart::host
