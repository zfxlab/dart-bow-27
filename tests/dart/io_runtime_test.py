"""Run production G4/vision receivers as Cortex-M7 code with transport stubs.

Requires build/h723-debug and build-local unicorn (see control_runtime_test.py).
Callbacks are captured through the real public init contracts, not copied parsers.
ThreadX scheduling, DMA, USB enumeration and physical I/O are not emulated.
"""
from pathlib import Path
import json
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/migration/io_emulation"
sys.path.insert(0, str(ROOT / "build/migration/python"))

HARNESS = r'''
#include "sensor.hpp"
#include "vision.hpp"
#include "dart_config.hpp"
#include "bsp_usart.hpp"
#include "bsp_gpio.hpp"
#include "bsp_usb.hpp"
#include "crc.hpp"
#include <cstring>
#include <cmath>
extern "C" {
volatile unsigned test_checks = 0, test_failures = 0, test_first_failure = 0;
__attribute__((noinline)) void test_done() { asm volatile("nop"); }
}
#define CHECK(x) do { ++test_checks; if (!(x)) { ++test_failures; if (!test_first_failure) test_first_failure = __LINE__; } } while (0)
namespace dart::topics {
msg::channel<sensor_data> sensor;
msg::channel<vision_rx> vision;
}
namespace {
bsp::usart::rx_callback uart_rx;
bsp::usb::config usb_cfg;
void (*sensor_entry)(ULONG);
ULONG tick;
unsigned sensor_publishes, vision_publishes, phase, restart_calls;
dart::sensor_data last_sensor;
dart::vision_rx last_vision;
types::status usb_result = types::status::ok;
std::uint8_t usb_bytes[64];
std::size_t usb_len;
std::uint8_t g4[] = {'L', 1, 2, 3, 'R', 4, 5, 6};
bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }
void feed(std::uint8_t* data, std::size_t len) { uart_rx(app::uart::force_sensor, {data, len}); }
}
namespace msg::detail {
types::status publish(topic_layout& layout, const void* data, std::size_t size, publish_opts) noexcept {
    if (&layout == &dart::topics::sensor.native_layout()) {
        CHECK(size == sizeof(last_sensor)); std::memcpy(&last_sensor, data, size); ++sensor_publishes;
    } else {
        CHECK(&layout == &dart::topics::vision.native_layout() && size == sizeof(last_vision));
        std::memcpy(&last_vision, data, size); ++vision_publishes;
    }
    return types::status::ok;
}
}
namespace bsp::usart {
types::status init(port p, mode m) { CHECK(p == app::uart::force_sensor && m == mode::dma); return types::status::ok; }
types::status start_rx_to_idle(port p, bsp::dma::buffer_view, rx_callback cb, TX_SEMAPHORE*) {
    CHECK(p == app::uart::force_sensor && cb.valid()); uart_rx = cb; return types::status::ok;
}
types::status restart_rx(port p) { CHECK(p == app::uart::force_sensor); ++restart_calls; return types::status::ok; }
}
namespace bsp::gpio {
types::status is_active(input p, bool& active) {
    CHECK(p == app::gpio::trigger_locked || p == app::gpio::launch_return);
    active = p == app::gpio::trigger_locked; return types::status::ok;
}
}
namespace bsp::usb {
types::status init(const config& c) { usb_cfg = c; return types::status::ok; }
types::status try_send(const std::uint8_t* data, std::size_t len) {
    CHECK(len <= sizeof(usb_bytes)); usb_len = len; std::memcpy(usb_bytes, data, len); return usb_result;
}
}
extern "C" {
ULONG _tx_time_get() { return tick; }
UINT _tx_thread_interrupt_control(UINT) { return 0; }
UINT _txe_thread_create(TX_THREAD*, CHAR*, VOID (*entry)(ULONG), ULONG, VOID*, ULONG,
                       UINT, UINT, ULONG, UINT, UINT) { sensor_entry = entry; return TX_SUCCESS; }
UINT _tx_thread_create(TX_THREAD*, CHAR*, VOID (*entry)(ULONG), ULONG, VOID*, ULONG,
                      UINT, UINT, ULONG, UINT) { sensor_entry = entry; return TX_SUCCESS; }
UINT _tx_thread_sleep(ULONG ticks) {
    using dart::sensor::debug;
    CHECK(ticks == dart::cfg::sensor::period_ticks);
    CHECK(last_sensor.is_trigger_locked && !last_sensor.is_launchplat_return);
    if (phase == 0) {
        CHECK(debug.frames == 0 && !last_sensor.force_online);
        feed(g4 + 4, 4); ++tick;
    } else if (phase == 1) {
        CHECK(debug.frames == 1 && debug.raw_l == 0x030201 && debug.raw_r == 0x060504);
        CHECK(last_sensor.force_online && near(last_sensor.string_l_force_kg, float(0x030201) / dart::cfg::sensor::force_scale));
        std::uint8_t bad[] = {'L', 0, 1, 2, 'X', 4, 5, 6}; feed(bad, sizeof(bad));
        std::uint8_t packets[] = {'L', 0x10, 0x27, 0, 'R', 0x20, 0x4e, 0,
                                  'L', 0x10, 0x27, 0, 'R', 0x20, 0x4e, 0};
        feed(packets, 3); feed(packets + 3, sizeof(packets) - 3); ++tick;
    } else if (phase == 2) {
        CHECK(debug.frames == 3 && last_sensor.force_online);
        CHECK(near(last_sensor.string_l_force_kg, 1) && near(last_sensor.string_r_force_kg, 2));
        tick += dart::cfg::sensor::timeout_ticks;
    } else if (phase == 3) {
        CHECK(!last_sensor.force_online && restart_calls == 1 && debug.restarts == 1);
        CHECK(near(last_sensor.string_l_force_kg, 1) && near(last_sensor.string_r_force_kg, 2));
        feed(g4, sizeof(g4)); ++tick;
    } else {
        CHECK(debug.frames == 4 && last_sensor.force_online && restart_calls == 1);
        CHECK(sensor_publishes == 5 && debug.loops == 5);
        test_done(); for (;;) {}
    }
    ++phase; return TX_SUCCESS;
}
}
void test_vision() {
    using namespace dart;
    CHECK(vision::init() == types::status::ok);
    CHECK(usb_cfg.on_rx_callback.valid() && usb_cfg.on_tx_result_callback.valid());
    vision_rx packet{0xa5, 1, 2, -1.25f, 4.5f, 0};
    auto* bytes = reinterpret_cast<std::uint8_t*>(&packet);
    crc::append_crc16_checksum(bytes, sizeof(packet));
    std::uint8_t noise[] = {0, 1, 2, 0x5a}; usb_cfg.on_rx_callback(noise, sizeof(noise));
    CHECK(vision::debug.rx_frames == 0);
    for (unsigned split = 0; split <= sizeof(packet); ++split) {
        const unsigned before = vision::debug.rx_frames;
        usb_cfg.on_rx_callback(bytes, split);
        if (split < sizeof(packet)) CHECK(vision::debug.rx_frames == before);
        usb_cfg.on_rx_callback(bytes + split, sizeof(packet) - split);
        CHECK(vision::debug.rx_frames == before + 1);
        CHECK(near(last_vision.yaw, -1.25f) && near(last_vision.distance, 4.5f));
    }
    std::uint8_t pair[2 * sizeof(packet)]; std::memcpy(pair, bytes, sizeof(packet)); std::memcpy(pair + sizeof(packet), bytes, sizeof(packet));
    auto before = vision::debug.rx_frames; usb_cfg.on_rx_callback(pair, sizeof(pair));
    CHECK(vision::debug.rx_frames == before + 2);
    bytes[12] ^= 1; before = vision::debug.rx_frames;
    usb_cfg.on_rx_callback(bytes, sizeof(packet));
    CHECK(vision::debug.rx_frames == before + (cfg::vision::verify_crc ? 0 : 1));
    bytes[12] ^= 1; usb_cfg.on_rx_callback(bytes, sizeof(packet));
    CHECK(vision::debug.rx_frames == before + (cfg::vision::verify_crc ? 1 : 2));
    CHECK(vision_publishes == vision::debug.rx_frames);
    CHECK(cfg::vision::verify_crc ? vision::debug.crc_errors > 0 : vision::debug.crc_errors == 0);
    vision_tx tx{0x5a, 2, 3, 0.5f, -1.0f, 0}; vision::log_frame log{};
    vision::send(tx, log);
    CHECK(usb_len == sizeof(tx) + sizeof(log) && vision::debug.tx_queued == 1);
    CHECK(vision::debug.tx_status == types::status::ok);
    CHECK(crc::verify_crc16_checksum(usb_bytes, sizeof(tx)));
    CHECK(crc::verify_crc16_checksum(usb_bytes + sizeof(tx), sizeof(log)));
    usb_result = types::status::busy; vision::send(tx, log);
    CHECK(vision::debug.tx_busy == 1 && vision::debug.tx_queued == 1);
    CHECK(vision::debug.tx_status == types::status::busy);
    usb_result = types::status::not_connected; vision::send(tx, log);
    CHECK(vision::debug.tx_status == types::status::not_connected && vision::debug.tx_queued == 1);
    bsp::usb::tx_result result{}; usb_cfg.on_tx_result_callback(result);
    CHECK(vision::debug.tx_errors == 1);
    result.code = bsp::usb::tx_result_code::success; usb_cfg.on_tx_result_callback(result);
    CHECK(vision::debug.tx_errors == 1);
}
extern "C" void test_entry() {
    test_vision();
    CHECK(dart::sensor::init() == types::status::ok && sensor_entry);
    std::uint8_t noise[] = {1, 2, 3}; feed(noise, sizeof(noise));
    feed(g4, 4); sensor_entry(0);
    test_done();
}
'''


def run_case(verify_crc, common, compiler, arch):
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M7, UC_ARM_REG_SP, UC_ARM_REG_LR,
                                   UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)
    case = OUT / ("crc_on" if verify_crc else "legacy_crc_off")
    case.mkdir(parents=True, exist_ok=True)
    config = (ROOT / "build/h723-debug/generated/dart_config.hpp").read_text()
    config = config.replace("verify_crc = false", "verify_crc = true" if verify_crc else "verify_crc = false")
    (case / "dart_config.hpp").write_text(config)
    flags = ["-I" + str(case)] + common + arch + ["-std=c++17", "-O1", "-g", "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
    harness = case / "io_harness.cpp"
    harness.write_text(HARNESS, encoding="utf-8")
    objects = []
    for path in [ROOT / "app/dart/sensor.cpp", ROOT / "app/dart/vision.cpp", ROOT / "pnx_libs/crc/src/crc.cpp", harness]:
        obj = case / f"{path.stem}.o"
        subprocess.run([compiler] + flags + ["-c", str(path), "-o", str(obj)], check=True, cwd=ROOT)
        objects.append(str(obj))
    script = case / "test.ld"
    script.write_text("ENTRY(test_entry)\nSECTIONS { . = 0x10000; .text : { *(.text*) *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.init_array*) *(.fini_array*) } }\n")
    elf = case / "io.elf"
    subprocess.run([compiler] + arch + ["-nostartfiles", "-Wl,--gc-sections", "-T", str(script)] + objects + ["-lc", "-lgcc", "-lnosys", "-o", str(elf)], check=True)
    prefix = str(Path(compiler).parent / "arm-none-eabi-")
    nm = subprocess.check_output([prefix + "nm.exe", "-n", str(elf)], text=True)
    symbols = {m[2]: int(m[0], 16) for line in nm.splitlines() if len(m := line.split()) == 3}
    binary = case / "io.bin"
    subprocess.run([prefix + "objcopy.exe", "-O", "binary", str(elf), str(binary)], check=True)
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M7)
    uc.mem_map(0x10000, 0x200000)
    uc.mem_write(0x10000, binary.read_bytes())
    uc.mem_map(0x3000000, 0x10000)
    uc.reg_write(UC_ARM_REG_SP, 0x300FFF0)
    uc.reg_write(UC_ARM_REG_LR, symbols["test_done"] | 1)
    uc.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
    uc.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
    done = False
    def stop(engine, address, size, data):
        nonlocal done
        if address == symbols["test_done"]:
            done = True
            engine.emu_stop()
    uc.hook_add(UC_HOOK_CODE, stop)
    uc.emu_start(symbols["test_entry"] | 1, 0x210000, count=3000000)
    read = lambda name: int.from_bytes(uc.mem_read(symbols[name], 4), "little")
    result = {"verify_crc": verify_crc, "completed": done, "checks": read("test_checks"), "failures": read("test_failures"), "first_failure_line": read("test_first_failure")}
    (case / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))
    if not done or result["failures"]:
        raise SystemExit(1)


def main():
    commands = json.loads((ROOT / "build/h723-debug/compile_commands.json").read_text())
    command = next(c["command"] for c in commands if "dart" in c["file"] and "sensor.cpp" in c["file"])
    args = [v.strip('"') for v in shlex.split(command, posix=False)]
    common = [v for v in args[1:] if v.startswith("-I") or v.startswith("-D")]
    arch = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    for verify_crc in (False, True):
        run_case(verify_crc, common, args[0], arch)
    print("PASS: production G4/vision callbacks and sensor loop executed as ARM M7 code; transport/time/message stubs, no hardware claim.")


if __name__ == "__main__":
    main()
