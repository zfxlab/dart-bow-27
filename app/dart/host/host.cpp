#include "host.hpp"
#include "internal.hpp"
#include "dart_config.hpp"
#include "bsp_dma.hpp"
#include "bsp_usart.hpp"
#include "crc.hpp"
#include "memory.h"
#include "tx_api.h"

#include <atomic>
#include <cstring>

namespace dart::host
{
debug_state debug{};
namespace
{
TX_THREAD thread{};
alignas(8) uint8_t stack[1024]{};
TX_SEMAPHORE rx_sem{};
bool started = false;
bsp::dma::buffer<256> rx_dma BSP_DMA_BUFFER{};
uint8_t rx_ring[512]{};
std::atomic<uint16_t> rx_write{0}, rx_read{0};

// Private bounded FIFOs: commands/events must not use latest-value msg channels.
// Queue access is from threads only; the ISR only writes rx_ring.
template<typename T, std::size_t N>
struct fifo
{
    T data[N]{};
    std::size_t head = 0, count = 0;
    TX_MUTEX lock{};

    bool push(const T* items, std::size_t n)
    {
        if (tx_mutex_get(&lock, TX_NO_WAIT) != TX_SUCCESS) { return false; }
        const bool fits = n <= N - count;
        if (fits)
        {
            for (std::size_t i = 0; i < n; ++i) { data[(head + count + i) % N] = items[i]; }
            count += n;
        }
        tx_mutex_put(&lock);
        return fits;
    }

    bool pop(T& item)
    {
        if (tx_mutex_get(&lock, TX_NO_WAIT) != TX_SUCCESS) { return false; }
        const bool present = count != 0;
        if (present) { item = data[head]; head = (head + 1) % N; --count; }
        tx_mutex_put(&lock);
        return present;
    }
};
fifo<request, 16> requests{};
fifo<response, 32> responses{};

enum class parse_state : uint8_t { sof1, sof2, header, payload, crc_low, crc_high };
enum class decode_result : uint8_t { ok, unknown_type, length_error };
struct frame
{
    uint8_t version{}, type{}, seq{}, len{};
    uint8_t payload[HOSTCOMM_MAX_PAYLOAD_SIZE]{};
};
parse_state parser_state = parse_state::sof1;
frame parser_frame{};
uint8_t parser_header_index{}, parser_payload_index{}, parser_crc_lo{};
ULONG parser_last_tick{};

void reset_parser()
{
    parser_state = parse_state::sof1;
    parser_frame = frame{};
    parser_header_index = 0;
    parser_payload_index = 0;
    parser_crc_lo = 0;
    parser_last_tick = tx_time_get();
}

void touch_parser()
{
    if (parser_state != parse_state::sof1)
    {
        parser_last_tick = tx_time_get();
    }
}

void check_timeout()
{
    if (parser_state == parse_state::sof1)
    {
        return;
    }

    if (tx_time_get() - parser_last_tick >= cfg::host::frame_timeout_ticks)
    {
        debug.parser_timeouts++;
        reset_parser();
    }
}

void put_u16(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void send_error(uint8_t ref_type, uint8_t ref_seq, uint8_t error, uint16_t detail)
{
    response rsp{};
    rsp.valid = 1;
    rsp.type = HOST_TYPE_ERROR_RSP;
    rsp.seq = ref_seq;
    rsp.len = 6;
    rsp.payload[0] = ref_type;
    rsp.payload[1] = ref_seq;
    rsp.payload[2] = error;
    put_u16(&rsp.payload[4], detail);
    if (!push_responses(&rsp, 1)) { ++debug.response_full; }
}

float read_float(const uint8_t* payload, uint8_t offset)
{
    float value = 0.0f;
    memcpy(&value, payload + offset, sizeof(value));
    return value;
}

decode_result decode_frame(const frame& frame, request* req)
{
    if (req == nullptr)
    {
        return decode_result::length_error;
    }

    memset(req, 0, sizeof(*req));
    req->valid = 1;
    req->seq = frame.seq;
    req->type = frame.type;

    switch (frame.type)
    {
        case HOST_TYPE_HEARTBEAT_REQ:
            if (frame.len != 0) { return decode_result::length_error; }
            req->kind = HOST_REQ_HEARTBEAT;
            return decode_result::ok;

        case HOST_TYPE_GET_DART_TABLE:
            if (frame.len != 0) { return decode_result::length_error; }
            req->kind = HOST_REQ_GET_DART_TABLE;
            return decode_result::ok;

        case HOST_TYPE_SET_DART_PARAMS_BATCH:
        {
            if (frame.len < 12) { return decode_result::length_error; }
            const uint8_t dart_count = frame.payload[2];
            if (dart_count == 0 || dart_count > 8 || frame.len != static_cast<uint8_t>(11U + dart_count))
            {
                return decode_result::length_error;
            }

            req->kind = HOST_REQ_SET_DART_PARAMS_BATCH;
            req->target_type = frame.payload[0];
            req->update_mask = frame.payload[1];
            req->dart_count = dart_count;
            memcpy(req->dart_ids, &frame.payload[3], dart_count);
            req->yaw_offset = read_float(frame.payload, static_cast<uint8_t>(3U + dart_count));
            req->tension = read_float(frame.payload, static_cast<uint8_t>(7U + dart_count));
            return decode_result::ok;
        }

        case HOST_TYPE_GET_SEQUENCE:
            if (frame.len != 0) { return decode_result::length_error; }
            req->kind = HOST_REQ_GET_SEQUENCE;
            return decode_result::ok;

        case HOST_TYPE_SET_SEQUENCE:
            if (frame.len != 4) { return decode_result::length_error; }
            req->kind = HOST_REQ_SET_SEQUENCE;
            memcpy(req->sequence, frame.payload, sizeof(req->sequence));
            return decode_result::ok;

        case HOST_TYPE_GET_PRE_TENSION:
            if (frame.len != 0) { return decode_result::length_error; }
            req->kind = HOST_REQ_GET_PRE_TENSION;
            return decode_result::ok;

        case HOST_TYPE_SET_PRE_TENSION:
            if (frame.len != 4) { return decode_result::length_error; }
            req->kind = HOST_REQ_SET_PRE_TENSION;
            req->pre_tension = read_float(frame.payload, 0);
            return decode_result::ok;

        case HOST_TYPE_RESET_TEST_ROUND:
            if (frame.len != 0) { return decode_result::length_error; }
            req->kind = HOST_REQ_RESET_TEST_ROUND;
            return decode_result::ok;

        default:
            req->kind = HOST_REQ_NONE;
            return decode_result::unknown_type;
    }
}

void handle_frame(const frame& frame)
{
    debug.rx_frames_ok++;

    if (frame.version != HOSTCOMM_PROTOCOL_VERSION)
    {
        send_error(frame.type, frame.seq, HOST_ERR_VERSION_UNSUPPORTED, frame.version);
        return;
    }

    request req{};
    const decode_result result = decode_frame(frame, &req);
    if (result != decode_result::ok)
    {
        if (result == decode_result::length_error)
        {
            debug.rx_length_errors++;
            send_error(frame.type, frame.seq, HOST_ERR_LENGTH_ERROR, frame.len);
        }
        else
        {
            send_error(frame.type, frame.seq, HOST_ERR_UNKNOWN_TYPE, frame.type);
        }
        return;
    }

    if (!requests.push(&req, 1))
    {
        ++debug.request_full;
        send_error(frame.type, frame.seq, HOST_ERR_QUEUE_FULL, 0);
    }
}

void set_header_byte(uint8_t index, uint8_t byte)
{
    switch (index)
    {
        case 0:
            parser_frame.version = byte;
            break;
        case 1:
            parser_frame.type = byte;
            break;
        case 2:
            parser_frame.seq = byte;
            break;
        case 3:
            parser_frame.len = byte;
            break;
        default:
            break;
    }
}

void parse_byte(uint8_t byte)
{
    switch (parser_state)
    {
        case parse_state::sof1:
            if (byte == HOSTCOMM_SOF1)
            {
                parser_state = parse_state::sof2;
                parser_frame = frame{};
                parser_header_index = 0;
                parser_payload_index = 0;
            }
            break;

        case parse_state::sof2:
            if (byte == HOSTCOMM_SOF2)
            {
                parser_state = parse_state::header;
                parser_header_index = 0;
                parser_frame = frame{};
            }
            else if (byte == HOSTCOMM_SOF1)
            {
                parser_state = parse_state::sof2;
            }
            else
            {
                reset_parser();
            }
            break;

        case parse_state::header:
            set_header_byte(parser_header_index, byte);
            parser_header_index++;
            if (parser_header_index >= 4)
            {
                if (parser_frame.len > HOSTCOMM_MAX_PAYLOAD_SIZE)
                {
                    debug.rx_length_errors++;
                    send_error(parser_frame.type, parser_frame.seq,
                                 HOST_ERR_LENGTH_ERROR, parser_frame.len);
                    reset_parser();
                    break;
                }
                parser_payload_index = 0;
                parser_state = (parser_frame.len == 0) ? parse_state::crc_low : parse_state::payload;
            }
            break;

        case parse_state::payload:
            parser_frame.payload[parser_payload_index++] = byte;
            if (parser_payload_index >= parser_frame.len)
            {
                parser_state = parse_state::crc_low;
            }
            break;

        case parse_state::crc_low:
            parser_crc_lo = byte;
            parser_state = parse_state::crc_high;
            break;

        case parse_state::crc_high:
        {
            uint8_t crc_data[4 + HOSTCOMM_MAX_PAYLOAD_SIZE] = {0};
            crc_data[0] = parser_frame.version;
            crc_data[1] = parser_frame.type;
            crc_data[2] = parser_frame.seq;
            crc_data[3] = parser_frame.len;
            memcpy(&crc_data[4], parser_frame.payload, parser_frame.len);
            const uint16_t expected = crc::get_crc16_modbus_checksum(crc_data, 4U + parser_frame.len, 0xFFFF);
            const uint16_t received = static_cast<uint16_t>(parser_crc_lo | (static_cast<uint16_t>(byte) << 8U));
            if (expected == received)
            {
                handle_frame(parser_frame);
            }
            else
            {
                debug.rx_crc_errors++;
                send_error(parser_frame.type, parser_frame.seq, HOST_ERR_CRC_ERROR, received);
            }
            reset_parser();
            break;
        }
    }

    touch_parser();
}


void on_rx(bsp::usart::port, const bsp::usart::rx_frame& frame) noexcept
{
    for (std::size_t i = 0; i < frame.len; ++i)
    {
        const uint16_t pos = rx_write.load(std::memory_order_relaxed);
        const uint16_t next = (pos + 1U) % sizeof(rx_ring);
        if (next == rx_read.load(std::memory_order_acquire))
        {
            ++debug.rx_ring_overflow;
            continue;
        }
        rx_ring[pos] = frame.data[i];
        rx_write.store(next, std::memory_order_release);
        ++debug.rx_bytes;
    }
    tx_semaphore_put(&rx_sem);
}

void send_next()
{
    static response pending{};
    if (!pending.valid && !responses.pop(pending)) { return; }
    uint8_t bytes[8 + HOSTCOMM_MAX_PAYLOAD_SIZE]{};
    bytes[0] = HOSTCOMM_SOF1;
    bytes[1] = HOSTCOMM_SOF2;
    bytes[2] = HOSTCOMM_PROTOCOL_VERSION;
    bytes[3] = pending.type;
    bytes[4] = pending.seq;
    bytes[5] = pending.len;
    memcpy(bytes + 6, pending.payload, pending.len);
    const uint16_t sum = crc::get_crc16_modbus_checksum(bytes + 2, 4 + pending.len, 0xFFFF);
    put_u16(bytes + 6 + pending.len, sum);
    // The BSP copies into its own DMA stage before returning.
    const auto result = bsp::usart::transmit(app::uart::host, bytes, 8 + pending.len, 0);
    if (result == types::status::busy) { ++debug.tx_busy; return; }
    if (result == types::status::ok) { ++debug.tx_queued; }
    else { ++debug.tx_errors; }
    pending = {};
}

void entry(ULONG)
{
    for (;;)
    {
        tx_semaphore_get(&rx_sem, cfg::host::period_ticks);
        auto pos = rx_read.load(std::memory_order_relaxed);
        while (pos != rx_write.load(std::memory_order_acquire))
        {
            const uint8_t byte = rx_ring[pos];
            pos = (pos + 1U) % sizeof(rx_ring);
            rx_read.store(pos, std::memory_order_release);
            parse_byte(byte);
        }
        check_timeout();
        send_next();
        tx_thread_sleep(cfg::host::period_ticks);
    }
}
} // namespace

bool pop_request(request& req) noexcept { return requests.pop(req); }
bool push_responses(const response* data, std::size_t count) noexcept
{
    return responses.push(data, count);
}

types::status init() noexcept
{
    if (started) { return types::status::ok; }
    if (tx_mutex_create(&requests.lock, const_cast<CHAR*>("host_req"), TX_INHERIT) != TX_SUCCESS ||
        tx_mutex_create(&responses.lock, const_cast<CHAR*>("host_rsp"), TX_INHERIT) != TX_SUCCESS ||
        tx_semaphore_create(&rx_sem, const_cast<CHAR*>("host_rx"), 0) != TX_SUCCESS)
    {
        return debug.init_status = types::status::error;
    }
    auto result = init_service();
    if (result != types::status::ok) { return debug.init_status = result; }
    result = bsp::usart::init(app::uart::host, bsp::usart::mode::dma);
    if (result != types::status::ok) { return debug.init_status = result; }
    reset_parser();
    result = bsp::usart::start_rx_to_idle(app::uart::host, rx_dma.view(),
                                         bsp::usart::rx_callback::bind<&on_rx>());
    if (result != types::status::ok) { return debug.init_status = result; }
    if (tx_thread_create(&thread, const_cast<CHAR*>("dart_host"), entry, 0,
                         stack, sizeof(stack), cfg::host::thread_priority, cfg::host::thread_priority,
                         TX_NO_TIME_SLICE, TX_AUTO_START) != TX_SUCCESS)
    {
        return debug.init_status = types::status::error;
    }
    started = true;
    return debug.init_status = types::status::ok;
}
} // namespace dart::host
