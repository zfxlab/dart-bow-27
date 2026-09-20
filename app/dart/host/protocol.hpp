#pragma once

#include <cstdint>

namespace dart::host
{
// oldframe protocol v1: AA 55, version/type/seq/len, payload, Modbus CRC16.
#define HOSTCOMM_SOF1 0xAAU
#define HOSTCOMM_SOF2 0x55U
#define HOSTCOMM_PROTOCOL_VERSION 0x01U
#define HOSTCOMM_MAX_PAYLOAD_SIZE 64U

typedef enum
{
    HOST_TYPE_HEARTBEAT_REQ = 0x01,
    HOST_TYPE_HEARTBEAT_RSP = 0x02,
    HOST_TYPE_GET_DART_TABLE = 0x10,
    HOST_TYPE_SET_DART_PARAMS_BATCH = 0x11,
    HOST_TYPE_GET_SEQUENCE = 0x12,
    HOST_TYPE_SET_SEQUENCE = 0x13,
    HOST_TYPE_GET_PRE_TENSION = 0x14,
    HOST_TYPE_SET_PRE_TENSION = 0x15,
    HOST_TYPE_RESET_TEST_ROUND = 0x16,
    HOST_TYPE_DART_PARAM_STATE = 0x20,
    HOST_TYPE_SEQUENCE_STATE = 0x21,
    HOST_TYPE_PRE_TENSION_STATE = 0x22,
    HOST_TYPE_DART_RUNTIME_STATE = 0x23,
    HOST_TYPE_DOOR_STATE = 0x24,
    HOST_TYPE_LAUNCHER_STATE = 0x25,
    HOST_TYPE_FAST_TELEMETRY = 0x26,
    HOST_TYPE_ACK = 0x70,
    HOST_TYPE_ERROR_RSP = 0x7F,
} HOST_FRAME_TYPE;

typedef enum
{
    HOST_REQ_NONE = 0,
    HOST_REQ_HEARTBEAT,
    HOST_REQ_GET_DART_TABLE,
    HOST_REQ_SET_DART_PARAMS_BATCH,
    HOST_REQ_GET_SEQUENCE,
    HOST_REQ_SET_SEQUENCE,
    HOST_REQ_GET_PRE_TENSION,
    HOST_REQ_SET_PRE_TENSION,
    HOST_REQ_RESET_TEST_ROUND,
} HOST_REQ_KIND;

typedef enum
{
    HOST_ACK_APPLIED = 0,
    HOST_ACK_ACCEPTED = 1,
    HOST_ACK_NO_CHANGE = 2,
} HOST_ACK_RESULT;

typedef enum
{
    HOST_ERR_CRC_ERROR = 0x01,
    HOST_ERR_HEADER_ERROR = 0x02,
    HOST_ERR_VERSION_UNSUPPORTED = 0x03,
    HOST_ERR_LENGTH_ERROR = 0x04,
    HOST_ERR_INVALID_COMMAND = 0x05,
    HOST_ERR_STATE_FORBIDDEN = 0x06,
    HOST_ERR_DEVICE_BUSY = 0x07,
    HOST_ERR_PARAM_OUT_OF_RANGE = 0x08,
    HOST_ERR_PERMISSION_DENIED = 0x09,
    HOST_ERR_INTERNAL_ERROR = 0x0A,
    HOST_ERR_DUPLICATE_DART_ID = 0x0B,
    HOST_ERR_UNKNOWN_TYPE = 0x0C,
    HOST_ERR_QUEUE_FULL = 0x0D,
} HOST_ERROR_CODE;

struct request
{
    uint8_t valid;
    uint8_t seq;
    uint8_t type;
    uint8_t kind;

    uint8_t target_type;
    uint8_t update_mask;
    uint8_t dart_count;
    uint8_t dart_ids[8];

    float yaw_offset;
    float tension;
    float pre_tension;

    uint8_t sequence[4];
};

struct response
{
    uint8_t valid;
    uint8_t type;
    uint8_t seq;
    uint8_t len;
    uint8_t payload[HOSTCOMM_MAX_PAYLOAD_SIZE];
};


static_assert(sizeof(float) == 4);
} // namespace dart::host
