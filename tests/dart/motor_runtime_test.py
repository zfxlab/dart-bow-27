"""Execute production motor PID/watch and trigger timing on Cortex-M7 Unicorn.

Includes the real motor.cpp to reach its file-local operations. Link GC removes
the unused transport/thread code; the shared production PID is linked unchanged.
Only PWM endpoints are stubbed. This is not a scheduler or hardware test.
Run after configuring build/h723-debug and installing build-local Unicorn.
"""
from pathlib import Path
import json
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/migration/motor_emulation"
sys.path.insert(0, str(ROOT / "build/migration/python"))

HARNESS = r'''
#include "motor.cpp"
#include <cmath>
extern "C" {
volatile unsigned test_checks = 0, test_failures = 0, test_first_failure = 0;
__attribute__((noinline)) void test_done() { asm volatile("nop"); }
}
#define CHECK(x) do { ++test_checks; if (!(x)) { ++test_failures; if (!test_first_failure) test_first_failure = __LINE__; } } while (0)
unsigned pwm_starts, pwm_stops, pwm_width_calls;
std::uint32_t pwm_width;
types::status pwm_status = types::status::ok;
namespace bsp::pwm {
types::status start(channel c) { CHECK(c == app::pwm::trigger); ++pwm_starts; return pwm_status; }
types::status stop(channel c) { CHECK(c == app::pwm::trigger); ++pwm_stops; return pwm_status; }
types::status set_pulse_width_us(channel c, std::uint32_t us) {
    CHECK(c == app::pwm::trigger); ++pwm_width_calls; pwm_width = us; return pwm_status;
}
}
bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
void test_pid() {
    using namespace dart::motor;
    control::pid pid{0, 0, 0, 100, 100};
    volatile pid_settings tuning = settings(2, 0, 0, 100, 100);
    volatile loop_watch watch{};
    CHECK(tuning.mode == 0x45 && !tuning.ref_override && !tuning.reset);
    CHECK(near(update_pid(pid, tuning, 5, 2, watch), 6));
    CHECK(near(watch.ref, 5) && near(watch.fdb, 2) && near(watch.err, 3) && near(watch.out, 6));
    tuning.kp = 3;
    CHECK(near(update_pid(pid, tuning, 5, 2, watch), 9) && near(pid.kp, 3));
    tuning.ref_override = true; tuning.ref = 8;
    CHECK(near(update_pid(pid, tuning, -50, 2, watch), 18));
    CHECK(near(watch.ref, 8) && near(watch.err, 6));
    tuning.max_out = 5;
    CHECK(near(update_pid(pid, tuning, 0, 2, watch), 5) && near(pid.max_out, 5));
    tuning.ref_override = false;
    CHECK(near(update_pid(pid, tuning, 1, 2, watch), -3) && near(watch.ref, 1));

    tuning.kp = 0; tuning.ki = 1; tuning.kd = 0; tuning.max_out = 100;
    tuning.mode = static_cast<std::uint8_t>(control::pid_mode::position);
    CHECK(near(update_pid(pid, tuning, 1, 0, watch), 1));
    CHECK(near(update_pid(pid, tuning, 1, 0, watch), 2));
    tuning.reset = true;
    CHECK(near(update_pid(pid, tuning, 1, 0, watch), 1) && !tuning.reset);
    CHECK(near(update_pid(pid, tuning, 1, 0, watch), 2));
    tuning.mode = old_pid_mode;
    CHECK(near(update_pid(pid, tuning, 1, 0, watch), 0.5f));
    CHECK(near(update_pid(pid, tuning, 1, 0, watch), 1.5f));
    tuning.max_iout = 1;
    CHECK(near(update_pid(pid, tuning, 1, 0, watch), 1) && near(pid.max_iout, 1));
    tuning.ki = 0; tuning.kd = 10;
    tuning.mode = static_cast<std::uint8_t>(control::pid_mode::position);
    CHECK(near(update_pid(pid, tuning, 2, 1, watch), 10));
    CHECK(near(update_pid(pid, tuning, 2, 1, watch), 0));
    tuning.mode = old_pid_mode;
    CHECK(near(update_pid(pid, tuning, 3, 1, watch), 0));
    CHECK(near(update_pid(pid, tuning, 4, 1, watch), 0));
    CHECK(near(pid.kd, 10) && pid.mode == static_cast<control::pid_mode>(0x45));
}
void test_trigger() {
    using namespace dart::motor;
    const ULONG hold = dart::cfg::motor::trigger_hold_ticks;
    CHECK(hold == 1000 && dart::cfg::motor::trigger_open_us == 1750 && dart::cfg::motor::trigger_idle_us == 1670);
    update_trigger(false, 0);
    CHECK(pwm_starts == 0 && pwm_stops == 0 && debug.trigger_state == 0);
    update_trigger(true, 10);
    CHECK(pwm_starts == 1 && pwm_width == 1750 && debug.trigger_state == 1 && !debug.trigger_pwm_stopped);
    update_trigger(false, 11); update_trigger(true, 12);
    CHECK(pwm_starts == 1 && pwm_width_calls == 1);
    update_trigger(true, 10 + hold - 1);
    CHECK(pwm_width_calls == 1 && debug.trigger_state == 1);
    update_trigger(true, 10 + hold);
    CHECK(pwm_starts == 2 && pwm_width_calls == 2 && pwm_width == 1670 && debug.trigger_state == 2);
    update_trigger(true, 10 + hold * 2 - 1);
    CHECK(pwm_stops == 0 && debug.trigger_state == 2);
    update_trigger(true, 10 + hold * 2);
    CHECK(pwm_stops == 1 && debug.trigger_state == 0 && debug.trigger_pwm_stopped);
    update_trigger(true, 10 + hold * 3);
    CHECK(pwm_starts == 2 && pwm_stops == 1);
    update_trigger(false, 11 + hold * 3); update_trigger(true, 12 + hold * 3);
    CHECK(pwm_starts == 3 && pwm_width == 1750 && debug.trigger_state == 1);
    update_trigger(false, 12 + hold * 4); update_trigger(false, 12 + hold * 5);
    CHECK(pwm_stops == 2 && debug.trigger_pwm_stopped);
    // Unsigned elapsed time must preserve the two phases over tick wrap.
    const ULONG start = 0xfffffff0UL;
    update_trigger(true, start);
    CHECK(pwm_starts == 5 && pwm_width == 1750);
    update_trigger(false, static_cast<ULONG>(start + hold - 1));
    CHECK(debug.trigger_state == 1);
    update_trigger(false, static_cast<ULONG>(start + hold));
    CHECK(debug.trigger_state == 2 && pwm_width == 1670);
    update_trigger(false, static_cast<ULONG>(start + hold * 2));
    CHECK(debug.trigger_state == 0 && pwm_stops == 3);
    CHECK(debug.pwm_errors == 0);
    pwm_status = types::status::error;
    update_trigger(true, 3000);
    CHECK(debug.pwm_errors == 2);
    update_trigger(false, 3000 + hold); update_trigger(false, 3000 + hold * 2);
    CHECK(debug.pwm_errors == 5 && debug.trigger_pwm_stopped);
}
extern "C" void test_entry() {
    test_pid(); test_trigger(); test_done(); for (;;) {}
}
'''


def main():
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M7, UC_ARM_REG_SP, UC_ARM_REG_LR,
                                   UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)
    OUT.mkdir(parents=True, exist_ok=True)
    commands = json.loads((ROOT / "build/h723-debug/compile_commands.json").read_text())
    command = next(c["command"] for c in commands if "dart" in c["file"] and "motor.cpp" in c["file"])
    args = [v.strip('"') for v in shlex.split(command, posix=False)]
    compiler = args[0]
    common = [v for v in args[1:] if v.startswith("-I") or v.startswith("-D")]
    arch = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    flags = common + arch + ["-std=c++17", "-O1", "-g", "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
    harness = OUT / "motor_harness.cpp"
    harness.write_text(HARNESS, encoding="utf-8")
    objects = []
    for path in [ROOT / "pnx_libs/control/src/pid.cpp", ROOT / "pnx_libs/math/src/constrain.cpp", harness]:
        obj = OUT / f"{path.stem}.o"
        subprocess.run([compiler] + flags + ["-c", str(path), "-o", str(obj)], check=True, cwd=ROOT)
        objects.append(str(obj))
    script = OUT / "test.ld"
    script.write_text("ENTRY(test_entry)\nSECTIONS { . = 0x10000; .text : { *(.text*) *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.init_array*) *(.fini_array*) } }\n")
    elf = OUT / "motor.elf"
    subprocess.run([compiler] + arch + ["-nostartfiles", "-Wl,--gc-sections", "-T", str(script)] + objects + ["-lc", "-lgcc", "-lnosys", "-o", str(elf)], check=True)
    prefix = str(Path(compiler).parent / "arm-none-eabi-")
    nm = subprocess.check_output([prefix + "nm.exe", "-n", str(elf)], text=True)
    symbols = {m[2]: int(m[0], 16) for line in nm.splitlines() if len(m := line.split()) == 3}
    binary = OUT / "motor.bin"
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
    result = {"completed": done, "checks": read("test_checks"), "failures": read("test_failures"), "first_failure_line": read("test_first_failure")}
    (OUT / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))
    if not done or result["failures"]:
        raise SystemExit(1)
    print("PASS: production motor update_pid/update_trigger plus shared PID executed as ARM M7 code; PWM stubs only, no hardware/scheduler claim.")


if __name__ == "__main__":
    main()
