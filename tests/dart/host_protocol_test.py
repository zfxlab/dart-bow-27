"""Check host migration compatibility against oldframe, without a device.

This audits real migrated function bodies and compiles the current library CRC
against known vectors. It does not simulate ThreadX/UART or claim hardware I/O.
Run: python tests/dart/host_protocol_test.py
"""
from pathlib import Path
import argparse
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def old_source(baseline, path):
    return subprocess.check_output(
        ['git', 'show', f'{baseline}:{path}'], cwd=ROOT
    ).decode('utf-8-sig')


def function_body(text, name):
    start = text.index('{', text.index(name + '('))
    depth, end = 1, start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def normalized(text, names):
    for old, new in names.items():
        text = re.sub(r'\b' + old + r'\b', new, text)
    return re.sub(r'\s+', '', text)


def check_equivalent(old, new, functions, normalize):
    for old_name, new_name in functions:
        assert normalize(function_body(old, old_name)) == normalize(
            function_body(new, new_name)
        ), f'Migration changed {old_name}; review this difference explicitly'
        print(f'PASS: oldframe body preserved: {new_name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default='origin/oldframe')
    args = parser.parse_args()
    host = (ROOT / 'app/dart/host/host.cpp').read_text()
    service = (ROOT / 'app/dart/host/service.cpp').read_text()
    old_host = old_source(args.baseline, 'User/Task/Src/HostComm.cpp')
    old_service = old_source(args.baseline, 'User/Task/Src/DartHostService.cpp')
    old_protocol = old_source(args.baseline, 'User/Task/Inc/HostProtocol.hpp')
    protocol = (ROOT / 'app/dart/host/protocol.hpp').read_text()
    # All four enums retain ordering as well as explicit wire values.
    enum_pattern = r'typedef enum\s*\{(.*?)\}\s*\w+;'
    assert re.findall(enum_pattern, old_protocol, re.S) == re.findall(
        enum_pattern, protocol, re.S
    ), 'Host protocol command, request, ACK, or error enum changed'
    print('PASS: host protocol enum values/order match oldframe')

    parser_names = {
        'ResetParser': 'reset_parser', 'TouchParserTimer': 'touch_parser',
        'CheckParserTimeout': 'check_timeout', 'PutU16': 'put_u16',
        'ReadFloat': 'read_float', 'DecodeFrame': 'decode_frame',
        'HandleFrame': 'handle_frame', 'AssignHeaderByte': 'set_header_byte',
        'ParseByte': 'parse_byte', 'HostFrame': 'frame',
        'DecodeResult': 'decode_result', 'ParseState': 'parse_state',
        'WaitSof1': 'sof1', 'WaitSof2': 'sof2', 'WaitHeader': 'header',
        'WaitPayload': 'payload', 'WaitCrcLo': 'crc_low', 'WaitCrcHi': 'crc_high',
        'LengthError': 'length_error', 'UnknownType': 'unknown_type', 'Ok': 'ok',
        'host_comm_stats': 'debug', 'msg_hostreq_t': 'request',
        'Get_CRC16_Modbus_Check_Sum': 'crc::get_crc16_modbus_checksum',
        'HOSTCOMM_PARSER_TIMEOUT_TICKS': 'cfg::host::frame_timeout_ticks',
    }

    def normalize_parser(text):
        text = text.replace('PublishError(hosttx_topic, ', 'send_error(')
        text = text.replace('HandleFrame(parser_frame, hostreq_topic, hosttx_topic)',
                            'handle_frame(parser_frame)')
        return normalized(text, parser_names)

    check_equivalent(old_host, host, [
        ('ResetParser', 'reset_parser'), ('TouchParserTimer', 'touch_parser'),
        ('CheckParserTimeout', 'check_timeout'), ('DecodeFrame', 'decode_frame'),
        ('AssignHeaderByte', 'set_header_byte'), ('ParseByte', 'parse_byte'),
    ], normalize_parser)

    service_names = {
        'DartLibrary': 'dart_state', 'Dart_Config_t': 'dart_param',
        'msg_hostreq_t': 'request', 'msg_hosttx_t': 'response',
        'HostPutU16': 'put_u16', 'HostPutU32': 'put_u32', 'HostPutFloat': 'put_float',
        'HostSendAck': 'send_ack', 'HostSendError': 'send_error',
        'HostSendDartParamState': 'send_dart', 'HostSendSequenceState': 'send_sequence',
        'HostSendPreTensionState': 'send_pre_tension', 'HostSendHeartbeat': 'send_heartbeat',
        'HostFloatInRange': 'in_range', 'Update_Current_Dart_Id': 'update_dart_id',
        'autoAim': 'auto_aim', 'IsResetTestRoundAllowed': 'reset_allowed',
    }

    def normalize_service(text):
        text = text.replace('hosttx_topic_, ', '')
        text = text.replace('HostPublish(topic, tx)', 'queue_reply(tx)')
        text = text.replace(' || hosttx_topic_ == nullptr', '')
        return normalized(text, service_names)

    check_equivalent(old_service, service, [
        ('HostSendAck', 'send_ack'), ('HostSendError', 'send_error'),
        ('HostSendDartParamState', 'send_dart'), ('HostSendSequenceState', 'send_sequence'),
        ('HostSendPreTensionState', 'send_pre_tension'), ('HostSendHeartbeat', 'send_heartbeat'),
        ('HostFloatInRange', 'in_range'), ('DartHostService::ProcessRequest', 'process_request'),
    ], normalize_service)

    # Compile the actual CRC body, adding only constexpr to evaluate known
    # vectors in ARM GCC. No alternate checksum implementation is introduced.
    crc_source = (ROOT / 'pnx_libs/crc/src/crc.cpp').read_text()
    crc_body = function_body(crc_source, 'get_crc16_modbus_checksum')
    check = ROOT / 'build/dart-host-check'
    check.mkdir(parents=True, exist_ok=True)
    source = check / 'crc_vectors.cpp'
    source.write_text('''#include <cstdint>
#include "protocol.hpp"
constexpr uint16_t checksum(const uint8_t* _data, uint32_t length, uint16_t seed)
''' + crc_body + '''
constexpr uint8_t ascii[] = {'1','2','3','4','5','6','7','8','9'};
constexpr uint8_t heartbeat[] = {0x01,0x01,0x2a,0x00};
static_assert(checksum(ascii, sizeof(ascii), 0xffff) == 0x4b37);
static_assert(checksum(heartbeat, sizeof(heartbeat), 0xffff) == 0x784f);
static_assert(sizeof(dart::host::request) == 32);
static_assert(sizeof(dart::host::response) == 68);
''')
    compiler = shutil.which('arm-none-eabi-g++')
    if not compiler:
        raise SystemExit('ARM GCC is required for the CRC golden-vector check')
    subprocess.run([compiler, '-std=c++17', '-mcpu=cortex-m7', '-mthumb',
                    '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                    '-I' + str(ROOT / 'app/dart/host'), str(source)], check=True)
    print('PASS: current CRC C++ body evaluates to Modbus golden vectors in ARM GCC')
    print('No UART, ThreadX scheduling, USB, or board execution was performed.')


if __name__ == '__main__':
    main()
