"""Run actual host UART parser/service in M7 ARM emulation with public-I/O stubs.

Run after configuring build/h723-debug and installing local Unicorn:
python tests/dart/host_runtime_test.py
No target hardware or real ThreadX scheduling is simulated.
"""
from pathlib import Path
import json
import shlex
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/migration/host_emulation"
sys.path.insert(0, str(ROOT / "build/migration/python"))

HARNESS = r'''
#include "host.hpp"
#include "configuration.hpp"
#include "messages.hpp"
#include "bsp_usart.hpp"
#include "tx_api.h"
#include <cstring>
namespace dart::topics { msg::channel<sensor_data> sensor; msg::channel<motor_cmd> motor; }
namespace msg {
namespace detail { subscriber subscribe(topic_layout& layout) noexcept { return {&layout, 0}; } }
types::status read(subscriber, void*, std::size_t) noexcept { return types::status::empty; }
}
static VOID (*worker)(ULONG) = nullptr;
static bsp::usart::rx_callback callback{};
static dart::launcher_status launcher{};
extern "C" {
volatile unsigned init_status{}, tx_count{}, tx_attempts{}, busy_remaining{};
unsigned simulated_tick{}, input_len{};
unsigned tx_lens[128]{};
unsigned char tx_frames[128][72]{}, input_bytes[256]{};
ULONG _tx_time_get() { return simulated_tick; }
UINT _tx_mutex_create(TX_MUTEX*, CHAR*, UINT) { return TX_SUCCESS; }
UINT _tx_mutex_get(TX_MUTEX*, ULONG) { return TX_SUCCESS; }
UINT _tx_mutex_put(TX_MUTEX*) { return TX_SUCCESS; }
UINT _tx_semaphore_create(TX_SEMAPHORE*, CHAR*, ULONG) { return TX_SUCCESS; }
UINT _tx_semaphore_get(TX_SEMAPHORE*, ULONG) { return TX_SUCCESS; }
UINT _tx_semaphore_put(TX_SEMAPHORE*) { return TX_SUCCESS; }
UINT _tx_thread_create(TX_THREAD*, CHAR*, VOID (*entry)(ULONG), ULONG, VOID*, ULONG, UINT, UINT, ULONG, UINT) { worker = entry; return TX_SUCCESS; }
__attribute__((noinline)) UINT _tx_thread_sleep(ULONG) { asm volatile("nop"); return TX_SUCCESS; }
__attribute__((noinline)) void test_done() { asm volatile("nop"); }
}
namespace bsp::usart {
types::status init(port, mode) { return types::status::ok; }
types::status start_rx_to_idle(port, bsp::dma::buffer_view, rx_callback cb, TX_SEMAPHORE*) { callback = cb; return types::status::ok; }
types::status transmit(port, const uint8_t* data, size_t len, uint32_t) {
    ++tx_attempts;
    if (busy_remaining) { --busy_remaining; return types::status::busy; }
    if (tx_count >= 128 || len > 72) return types::status::error;
    std::memcpy(tx_frames[tx_count], data, len); tx_lens[tx_count] = len; ++tx_count;
    return types::status::ok;
}
}
extern "C" void test_init() {
    dart::state = dart::dart_state{};
    for (int i=1; i<=16; ++i) dart::state.config.dart[i] = {i, i * 0.1f, 60.0f+i, 40.0f+i};
    dart::state.config.pre_tension_kg = 32;
    init_status = static_cast<unsigned>(dart::host::init());
}
extern "C" void test_feed() { bsp::usart::rx_frame frame{input_bytes, input_len}; callback(app::uart::host, frame); }
extern "C" void test_worker() { worker(0); }
extern "C" void test_poll() { dart::host::poll(dart::state, launcher); }
'''


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def frame(kind, seq, payload=b"", version=1):
    body = bytes([version, kind, seq, len(payload)]) + payload
    return b"\xaa\x55" + body + struct.pack("<H", crc16(body))


def main():
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M7, UC_ARM_REG_SP, UC_ARM_REG_LR,
                                   UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)
    OUT.mkdir(parents=True, exist_ok=True)
    commands = json.loads((ROOT / "build/h723-debug/compile_commands.json").read_text())
    command = next(c["command"] for c in commands if "dart" in c["file"] and "host.cpp" in c["file"])
    args = [a.strip('"') for a in shlex.split(command, posix=False)]
    compiler = args[0]
    common = [a for a in args[1:] if a.startswith("-I") or a.startswith("-D")]
    arch = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    flags = common + arch + ["-std=c++17", "-O1", "-g", "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
    harness = OUT / "host_harness.cpp"
    harness.write_text(HARNESS)
    objects = []
    for path in [ROOT / "app/dart/host/host.cpp", ROOT / "app/dart/host/service.cpp", ROOT / "app/dart/configuration.cpp", ROOT / "pnx_libs/crc/src/crc.cpp", harness]:
        obj = OUT / f"{path.stem}.o"
        subprocess.run([compiler] + flags + ["-c", str(path), "-o", str(obj)], check=True, cwd=ROOT)
        objects.append(str(obj))
    script = OUT / "test.ld"
    script.write_text("ENTRY(test_init)\nSECTIONS { . = 0x10000; .text : { *(.text*) *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.init_array*) *(.fini_array*) } }\n")
    elf = OUT / "host.elf"
    roots = ["test_feed", "test_worker", "test_poll", "test_done"]
    subprocess.run([compiler] + arch + ["-nostartfiles", "-Wl,--gc-sections", "-T", str(script)] + [f"-Wl,-u,{name}" for name in roots] + objects + ["-lc", "-lgcc", "-lnosys", "-o", str(elf)], check=True)
    prefix = str(Path(compiler).parent / "arm-none-eabi-")
    nm = subprocess.check_output([prefix + "nm.exe", "-n", str(elf)], text=True)
    symbols = {m[2]: int(m[0], 16) for line in nm.splitlines() if len(m := line.split()) == 3}
    binary = OUT / "host.bin"
    subprocess.run([prefix + "objcopy.exe", "-O", "binary", str(elf), str(binary)], check=True)
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M7)
    uc.mem_map(0x10000, 0x200000); uc.mem_write(0x10000, binary.read_bytes())
    uc.mem_map(0x3000000, 0x10000)
    uc.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20); uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    stopped = False
    def stop(engine, address, size, data):
        nonlocal stopped
        if address in (symbols["test_done"], symbols["_tx_thread_sleep"]):
            stopped = True
            engine.emu_stop()
    uc.hook_add(UC_HOOK_CODE, stop)
    def call(name):
        nonlocal stopped
        stopped = False
        uc.reg_write(UC_ARM_REG_SP, 0x300FFF0); uc.reg_write(UC_ARM_REG_LR, symbols["test_done"] | 1)
        uc.emu_start(symbols[name] | 1, 0x210000, count=1000000)
        assert stopped, f"{name}: emulator instruction budget exhausted"
    def read(name): return int.from_bytes(uc.mem_read(symbols[name], 4), "little")
    def write(name, value): uc.mem_write(symbols[name], struct.pack("<I", value))
    def feed(data):
        uc.mem_write(symbols["input_bytes"], bytes(data)); write("input_len", len(data)); call("test_feed")
    def worker(n=1):
        for _ in range(n): call("test_worker")
    def sent(index):
        length = int.from_bytes(uc.mem_read(symbols["tx_lens"] + index * 4, 4), "little")
        data = bytes(uc.mem_read(symbols["tx_frames"] + index * 72, length))
        assert data[:2] == b"\xaa\x55" and len(data) == 8 + data[5]
        assert crc16(data[2:-2]) == int.from_bytes(data[-2:], "little")
        return data[3], data[4], data[6:-2]
    checks = []
    def check(name, ok):
        if not ok: raise AssertionError(name)
        checks.append(name)
    call("test_init"); check("real init captured thread and callback", read("init_status") == 0)
    # Split across header and CRC boundaries; poll cannot see an incomplete request.
    req = frame(0x01, 7)
    feed(req[:1]); worker(); call("test_poll"); check("partial SOF produces no reply", read("tx_count") == 0)
    feed(req[1:6]); worker(); call("test_poll"); check("partial CRC produces no reply", read("tx_count") == 0)
    feed(req[6:]); worker(); call("test_poll"); worker()
    check("fragmented heartbeat sequence retained", sent(0)[:2] == (0x02, 7))
    # Two frames in one callback must remain distinct ordered events.
    feed(frame(0x12, 8) + frame(0x14, 9)); worker(); call("test_poll"); worker(2)
    check("consecutive frames keep FIFO order", [sent(i)[:2] for i in (1,2)] == [(0x21,8),(0x22,9)])
    check("sequence payload preserved", sent(1)[2][2:] == bytes([1,2,3,4]))
    check("pre-tension payload preserved", struct.unpack("<f", sent(2)[2][2:])[0] == 32)
    bad = bytearray(frame(0x01, 10)); bad[-1] ^= 0x80
    feed(bad); worker(); call("test_poll")
    check("CRC error receives error reply", sent(3)[0] == 0x7F and sent(3)[2][2] == 1)
    # BSP busy must retain the exact pending message until retry succeeds.
    feed(frame(0x01, 11)); worker(); call("test_poll"); write("busy_remaining", 2)
    worker(2); check("busy does not consume reply", read("tx_count") == 4)
    worker(); check("busy retry sends original once", read("tx_count") == 5 and sent(4)[:2] == (2,11))
    # All sixteen table rows and final ACK must survive one burst.
    feed(frame(0x10, 12)); worker(); call("test_poll"); worker(17)
    rows = [sent(i) for i in range(5,21)]
    check("GET table sends 16 rows", all(r[0:2] == (0x20,12) for r in rows))
    check("GET table row IDs ordered", [r[2][2] for r in rows] == list(range(1,17)))
    check("GET table ACK follows final row", sent(21)[0:2] == (0x70,12))
    check("GET table row numeric data", struct.unpack("<ff", rows[3][2][7:])[0] == 64)
    # Valid setter must generate ACK then new state, through the real service.
    feed(frame(0x13, 13, bytes([3,4,5,8]))); worker(); call("test_poll"); worker(2)
    check("SET sequence ACK then state", [sent(i)[0] for i in (22,23)] == [0x70,0x21])
    check("SET sequence changes response", sent(23)[2][2:] == bytes([3,4,5,8]))
    # Timed-out partial input is discarded; a following frame resynchronizes.
    feed(frame(0x01, 14)[:4]); worker(); write("simulated_tick", 51); worker()
    feed(frame(0x01, 15)); worker(); call("test_poll"); worker(2)
    check("timeout recovery accepts following frame", sent(24)[0:2] == (2,15))
    # Two complete tables exceed the 32-reply queue. The second batch must
    # remain intact while a later setter waits behind it, then resume once
    # the UART consumer has drained capacity.
    start = read("tx_count")
    feed(frame(0x10, 16) + frame(0x10, 17) + frame(0x13, 18, bytes([9,10,11,12])))
    worker(); call("test_poll"); worker(20)
    check("queue pressure retains second batch", read("tx_count") == start + 17)
    check("first table remains complete under pressure", [sent(start+i)[:2] for i in range(17)] == [(0x20,16)]*16 + [(0x70,16)])
    call("test_poll"); worker(20)
    check("pending table resumes without losing replies", read("tx_count") == start + 36)
    check("pending table preserves row order", [sent(start+17+i)[2][2] for i in range(16)] == list(range(1,17)))
    check("later request remains behind complete table", [sent(start+i)[:2] for i in (33,34,35)] == [(0x70,17),(0x70,18),(0x21,18)])
    check("queued setter applied once", struct.unpack("<H", sent(start+35)[2][:2])[0] == 2 and sent(start+35)[2][2:] == bytes([9,10,11,12]))
    check("all emitted response CRCs valid", all(sent(i) for i in range(read("tx_count"))))
    result = {"passed": len(checks), "checks": checks, "tx_count": read("tx_count"), "tx_attempts": read("tx_attempts"), "scope": "real host.cpp/service.cpp/configuration.cpp/crc.cpp; UART, locks, time, and scheduler are stubs"}
    (OUT / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == "__main__": main()
