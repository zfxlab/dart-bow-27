"""Execute actual dart business sources as Cortex-M7 Thumb/hard-float in Unicorn.

Requires a configured build/h723-debug and local optional emulator dependency:
python -m pip install --target build/migration/python unicorn
python tests/dart/control_runtime_test.py

The harness replaces only ThreadX time and excludes unused thread/transport
sections at link time. It does not emulate peripherals or scheduler behavior.
"""
from pathlib import Path
import json
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/migration/control_emulation"
sys.path.insert(0, str(ROOT / "build/migration/python"))

HARNESS = r'''
#include "launcher.hpp"
#include "control.hpp"
#include "configuration.hpp"
#include "dart_config.hpp"
#include <cmath>
extern "C" {
volatile unsigned test_checks = 0, test_failures = 0, test_first_failure = 0;
ULONG simulated_tick = 0;
ULONG _tx_time_get() { return simulated_tick; }
__attribute__((noinline)) void test_done() { asm volatile("nop"); }
}
#define CHECK(x) do { ++test_checks; if (!(x)) { ++test_failures; if (!test_first_failure) test_first_failure = __LINE__; } } while (0)
bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }

void test_delay() {
    dart::delay d;
    simulated_tick = 0xfffffff0UL; CHECK(!d.reached(32));
    simulated_tick = 0x0000000fUL; CHECK(!d.reached(32));
    simulated_tick = 0x00000010UL; CHECK(d.reached(32));
    d.reset(); simulated_tick = 100; CHECK(!d.stable(true, 10));
    simulated_tick = 109; CHECK(!d.stable(false, 10));
    simulated_tick = 110; CHECK(!d.stable(true, 10));
    simulated_tick = 119; CHECK(!d.stable(true, 10));
    simulated_tick = 120; CHECK(d.stable(true, 10));
    d.reset(); simulated_tick = 200; CHECK(!d.latched(10));
    simulated_tick = 210; CHECK(d.latched(10));
    simulated_tick = 211; CHECK(d.latched(10));
}

void test_remote() {
    using namespace dart;
    using sw = remoter::sw_state;
    remoter::state rc{}; command cmd{};
    rc.left_sw = sw::low; rc.right_sw = sw::low;
    control::map_remote(rc, cmd); CHECK(cmd.action == action::relax);
    rc.right_sw = sw::mid; rc.right_y = 0.5f; cmd = {};
    control::map_remote(rc, cmd); CHECK(cmd.action == action::syn_adjust); CHECK(near(cmd.rc_syn, -0.005f));
    rc.right_sw = sw::up; rc.left_y = -0.2f; rc.right_y = 0.3f; cmd = {};
    control::map_remote(rc, cmd); CHECK(cmd.action == action::string_adjust);
    CHECK(near(cmd.rc_string_l, -0.2f) && near(cmd.rc_string_r, 0.3f));
    rc.left_sw = sw::mid; rc.right_sw = sw::low; rc.left_x = 0.8f; rc.right_x = -0.3f; cmd = {};
    control::map_remote(rc, cmd); CHECK(cmd.action == action::trigger_open); CHECK(near(cmd.yaw, -0.3f));
    rc.left_x = 0.7f; cmd = {}; control::map_remote(rc, cmd); CHECK(cmd.action == action::trigger_close);
    rc.right_sw = sw::mid; cmd = {}; control::map_remote(rc, cmd); CHECK(cmd.action == action::prepare);
    rc.right_sw = sw::up; cmd = {}; control::map_remote(rc, cmd); CHECK(cmd.action == action::fire);
}

void test_auto() {
    using namespace dart;
    state = dart_state{};
    state.runtime.vision_door_status = door_state::open;
    vision_rx vision{}; vision.stable_state = 1;
    command cmd{};
    const float errors[] = {0.2f, -0.06f, 0.02f};
    const float velocities[] = {1.0f, -0.5f, 0.15f};
    for (int i = 0; i < 3; ++i) {
        cmd = {}; vision.yaw = errors[i]; control::auto_command(vision, cmd);
        CHECK(cmd.action == action::prepare); CHECK(near(cmd.yaw, velocities[i]));
    }
    cmd = {}; vision.yaw = 0; control::auto_command(vision, cmd);
    CHECK(cmd.action == action::fire && state.runtime.auto_aim.yaw_ok);
    cmd = {}; vision.yaw = 666; control::auto_command(vision, cmd);
    CHECK(cmd.action == action::prepare && !state.runtime.auto_aim.yaw_ok && cmd.yaw == 0);
    state.runtime.fired_count_this_open = 2; cmd = {}; vision.yaw = 0;
    control::auto_command(vision, cmd); CHECK(cmd.action == action::prepare);
    state.runtime.current_shot_number = 5; cmd = {}; control::auto_command(vision, cmd);
    CHECK(cmd.action == action::relax);
}

void test_calibration() {
    using namespace dart;
    dart_state s{};
    s.config.dart[1] = {1, -1.0f, 50.0f, 40.0f};
    s.config.dart[3] = {3, -3.0f, 55.0f, 45.0f};
    table_point table[] = {{1, 30, 2, 80}, {2, 20, 99, 99}, {1, 10, 0, 60}};
    s.set_table(table, 3);
    auto a = s.lookup_aim(1, 20); CHECK(near(a.yaw_offset, 1) && near(a.tension_kg, 70));
    a = s.lookup_aim(1, 5); CHECK(near(a.yaw_offset, 0) && near(a.tension_kg, 60));
    a = s.lookup_aim(1, 40); CHECK(near(a.yaw_offset, 2) && near(a.tension_kg, 80));
    a = s.lookup_aim(3, 20); CHECK(near(a.yaw_offset, -3) && near(a.tension_kg, 55));
    a = s.lookup_aim(1, 0); CHECK(near(a.yaw_offset, -1) && near(a.tension_kg, 50));
    CHECK(s.prepare_slot() == slot::none);
    s.runtime.current_shot_number = 2; CHECK(s.prepare_slot() == slot::slot_1);
    s.runtime.current_shot_number = 4; CHECK(s.prepare_slot() == slot::slot_3);
    s.config.sequence[3] = 8; s.update_dart_id(); CHECK(s.runtime.current_dart_id == 8);
    launcher_status l{}; l.is_fire_finished = true;
    s.update_shot(&l); s.update_fired(&l); s.update_history(&l);
    CHECK(s.runtime.current_shot_number == 5 && s.runtime.fired_count_this_open == 1);
    s.update_shot(&l); s.update_fired(&l);
    CHECK(s.runtime.current_shot_number == 5 && s.runtime.fired_count_this_open == 1);
}

void test_door() {
    using namespace dart;
    dart_state s{}; vision_rx v{};
    s.runtime.referee.launch_station_status = 1;
    s.runtime.fired_count_this_open = 2;
    v.light_detected = 1;
    const ULONG open_at = cfg::system::vision_open_ticks + cfg::system::door_stable_ticks;
    for (simulated_tick = 0; simulated_tick < open_at; ++simulated_tick) { s.update_door(&v); s.update_prepare(); }
    CHECK(!s.runtime.auto_aim.autoaim_allow);
    s.update_door(&v); s.update_prepare();
    CHECK(s.runtime.auto_aim.autoaim_allow && s.runtime.fired_count_this_open == 0);
    const ULONG closed_start = simulated_tick + 1;
    v.light_detected = 0;
    const ULONG closed_at = closed_start + cfg::system::vision_closed_ticks + cfg::system::door_stable_ticks;
    for (simulated_tick = closed_start; simulated_tick < closed_at; ++simulated_tick) { s.update_door(&v); s.update_prepare(); }
    CHECK(s.runtime.auto_aim.autoaim_allow);
    s.update_door(&v); s.update_prepare(); CHECK(!s.runtime.auto_aim.autoaim_allow);
    // Referee-only open is sufficient, but closing one source is not sufficient.
    s.runtime.referee.launch_station_status = 0;
    const ULONG referee_start = simulated_tick + 1;
    for (simulated_tick = referee_start; simulated_tick <= referee_start + cfg::system::referee_stable_ticks + cfg::system::door_stable_ticks; ++simulated_tick) { s.update_door(&v); s.update_prepare(); }
    CHECK(s.runtime.auto_aim.autoaim_allow);
}

void test_launcher() {
    using namespace dart;
    using ls = launcher::state;
    using prep = launcher::prepare;
    launcher::debug = {};
    command cmd{}; sensor_data sensor{}; motor_fdb fdb{}; motor_cmd ref{}; launcher_status out{};
    cmd.action = action::prepare; cmd.current_shot_number = 1; cmd.tension_kg = 69;
    sensor.string_l_force_kg = sensor.string_r_force_kg = 69;
    simulated_tick = 0; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::preparing);
    simulated_tick = 1; launcher::step(cmd,sensor,fdb,ref,out); CHECK(near(ref.string_l_spd, cfg::launcher::string_relax_spd));
    simulated_tick += cfg::launcher::string_relax_ticks; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.prep == prep::syn_1);
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.prep == prep::trigger_ready);
    fdb.syn_pos_fdb = cfg::launcher::syn_pos_3;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.prep == prep::trigger_ready);
    simulated_tick += cfg::launcher::trigger_ready_ticks; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.prep == prep::tension);
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(!ref.string_able);
    fdb.syn_pos_fdb = cfg::launcher::syn_pos_5;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(ref.string_able && launcher::debug.fsm == ls::ready);
    cmd.action = action::fire;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::ready);
    simulated_tick += cfg::launcher::ready_fire_ticks - 1; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::ready);
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::firing);
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(ref.trigger_release && !ref.string_able);
    simulated_tick += cfg::launcher::firing_ticks; launcher::step(cmd,sensor,fdb,ref,out); CHECK(out.is_fire_finished && launcher::debug.fsm == ls::idle);
    cmd.action = action::relax;
    simulated_tick += cfg::launcher::fire_done_ticks; launcher::step(cmd,sensor,fdb,ref,out);
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(!out.is_fire_finished);
    // The inactive gantry must not be treated as valid even at its target.
    launcher::debug.fsm = ls::preparing; launcher::debug.prep = prep::gantry_1;
    launcher::debug.current_slot = slot::slot_2;
    cmd.action = action::prepare; fdb.gantry_pos_fdb = 0; fdb.gantry_online = false;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out);
    CHECK(launcher::debug.gantry_missing && launcher::debug.prep == prep::gantry_1);
    fdb.gantry_online = true;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.prep == prep::syn_2);
    // Non-first shots must pass through gantry instead of the preloaded path.
    launcher::debug.prep = prep::syn_1; cmd.current_shot_number = 2;
    fdb.syn_pos_fdb = cfg::launcher::syn_pos_1;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.prep == prep::gantry_1);
    // Explicit manual commands retain the old state override and reference.
    cmd.action = action::syn_adjust; cmd.rc_syn = 0.2f; ref.synbelt_pos = 2.0f;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out);
    CHECK(launcher::debug.fsm == ls::hand && near(ref.synbelt_pos, 2.2f));
    cmd.action = action::string_adjust; cmd.rc_string_l = -0.5f; cmd.rc_string_r = 0.6f;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out);
    CHECK(!ref.string_able && near(ref.string_l_spd, -0.5f) && near(ref.string_r_spd, 0.6f));
    cmd.action = action::trigger_open;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(ref.trigger_release);
    cmd.action = action::relax;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::hand && ref.trigger_release);
    cmd.action = action::trigger_close;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(!ref.trigger_release);
    cmd.action = action::relax;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::idle);
    // Existing error priority, and release into the existing manual path.
    sensor.string_l_force_kg = cfg::launcher::force_error_kg + 1.0f;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::error_stop);
    cmd.action = action::syn_adjust;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::error_stop);
    sensor.string_l_force_kg = 69;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.fsm == ls::hand);
    cmd.action = action::relax; fdb.syn_tq_fdb = cfg::launcher::syn_tq_error + 1.0f;
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(!launcher::debug.torque_error);
    simulated_tick += cfg::launcher::syn_tq_error_ticks - 1; launcher::step(cmd,sensor,fdb,ref,out); CHECK(!launcher::debug.torque_error);
    ++simulated_tick; launcher::step(cmd,sensor,fdb,ref,out); CHECK(launcher::debug.torque_error && launcher::debug.fsm == ls::error_stop);
}
extern "C" void test_entry() {
    test_delay(); test_remote(); test_auto(); test_calibration(); test_door(); test_launcher();
    test_done();
    for (;;) {}
}
'''


def main():
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
    from unicorn.arm_const import (UC_CPU_ARM_CORTEX_M7, UC_ARM_REG_SP, UC_ARM_REG_LR,
                                   UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC)
    OUT.mkdir(parents=True, exist_ok=True)
    commands = json.loads((ROOT / "build/h723-debug/compile_commands.json").read_text())
    command = next(c["command"] for c in commands if "dart" in c["file"] and "control.cpp" in c["file"])
    args = [v.strip('"') for v in shlex.split(command, posix=False)]
    compiler = args[0]
    common = [v for v in args[1:] if v.startswith("-I") or v.startswith("-D")]
    arch = ["-mcpu=cortex-m7", "-mthumb", "-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    flags = common + arch + ["-std=c++17", "-O1", "-g", "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"]
    harness = OUT / "business_harness.cpp"
    harness.write_text(HARNESS, encoding="utf-8")
    objects = []
    for path in [ROOT / "app/dart/launcher.cpp", ROOT / "app/dart/control.cpp", ROOT / "app/dart/configuration.cpp", harness]:
        obj = OUT / f"{path.stem}.o"
        subprocess.run([compiler] + flags + ["-c", str(path), "-o", str(obj)], check=True, cwd=ROOT)
        objects.append(str(obj))
    script = OUT / "test.ld"
    script.write_text("ENTRY(test_entry)\nSECTIONS { . = 0x10000; .text : { *(.text*) *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } /DISCARD/ : { *(.init_array*) *(.fini_array*) } }\n")
    elf = OUT / "business.elf"
    subprocess.run([compiler] + arch + ["-nostartfiles", "-Wl,--gc-sections", "-T", str(script)] + objects + ["-lc", "-lgcc", "-lnosys", "-o", str(elf)], check=True)
    prefix = str(Path(compiler).parent / "arm-none-eabi-")
    nm = subprocess.check_output([prefix + "nm.exe", "-n", str(elf)], text=True)
    symbols = {m[2]: int(m[0], 16) for line in nm.splitlines() if len(m := line.split()) == 3}
    binary = OUT / "business.bin"
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
    print("PASS: real launcher/control/configuration ARM M7 hard-float code; simulated time only, no hardware or scheduler claim.")


if __name__ == "__main__":
    main()
