"""Compile-time checks of the exact motor operations used by the ARM image.

Run: python tests/dart/test_motor.py
No board connection, flashing, or native compiler is required. ARM GCC evaluates
the shared constexpr functions and oldframe reference code in static_asserts.
This checks protocol bytes and arithmetic, not CAN timing or hardware behavior.
"""

from pathlib import Path
import argparse
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[2]


def old_source(ref: str, path: str) -> str:
    return subprocess.check_output(
        ["git", "show", f"{ref}:{path}"], cwd=ROOT, encoding="utf-8-sig"
    )


def reference_code(ref: str) -> str:
    # Keep the actual legacy wrap decisions, rather than restating the new code
    # in Python. The existing DM driver still supplies the same wrapped angle.
    source = old_source(ref, "User/Module/DMMotor/Src/DM4310_MultiPos.cpp")
    start = source.index("    if (!positionInited)", source.index("void DM4310_MultiPos::ReceiveData"))
    end = source.index("    this->motorFeedback.speedFdb", start)
    unwrap = source[start:end].replace(
        "this->motorFeedback.positionFdb = continuous_position;", "return continuous_position;"
    )

    # Reuse oldframe's packet splitting loop, replacing just HAL delivery with
    # a local frame capture. DLC is represented by byte count in this test.
    source = old_source(ref, "User/BSP/Src/bsp_can.cpp")
    start = source.index("    uint8_t i = 0, packNum = 0;", source.index("void can_SendCmd("))
    end = source.index("\n}", start)
    split = source[start:end]
    split = split.replace("uint8_t data[8];", "uint8_t data[8]{};")
    split = split.replace("FDCAN_TxHeaderTypeDef tx = {0};", "legacy_header tx{};\n    string_frames result{};")
    split = re.sub(r"^\s*tx\.(?:IdType|TxFrameType|ErrorStateIndicator|BitRateSwitch|FDFormat|TxEventFifoControl|MessageMarker)\s*=.*$", "", split, flags=re.M)
    split = split.replace("fdcan_len_to_dlc((uint8_t)(chunk + 1))", "static_cast<uint8_t>(chunk + 1)")
    split = split.replace(
        "HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &tx, data);",
        "auto& frame = packNum == 0 ? result.first : result.last;\n"
        "        frame.id = tx.Identifier; frame.len = tx.DataLength;\n"
        "        for (uint8_t j = 0; j < frame.len; ++j) frame.data[j] = data[j];",
    )
    return """
using uint8_t = std::uint8_t;
using uint32_t = std::uint32_t;
constexpr float Pi = math::pi;
struct legacy_position {
    float rawPositionFdb{}, lastRawPositionFdb{};
    std::int32_t positionRoundCount{};
    bool positionInited{};
    constexpr float update(float raw) {
        lastRawPositionFdb = rawPositionFdb;
        rawPositionFdb = raw;
""" + unwrap + """
    }
};
struct legacy_header { uint32_t Identifier{}; uint8_t DataLength{}; };
constexpr string_frames legacy_split(const uint8_t* cmd, uint8_t len) {
""" + split + """
    return result;
}
"""


CHECKS = r"""
constexpr bool near(float lhs, float rhs) {
    const float delta = lhs - rhs;
    return delta > -0.00005f && delta < 0.00005f;
}
constexpr bool same(const can_fragment& a, const can_fragment& b) {
    if (a.id != b.id || a.len != b.len) return false;
    for (unsigned i = 0; i < a.len; ++i) if (a.data[i] != b.data[i]) return false;
    return true;
}
constexpr bool golden_frames() {
    const auto left = make_string_frames(2, 0, 500.0f, 3000, 5000);
    const can_fragment first{0x200, 8, {0xC6, 0x00, 0x0B, 0xB8, 0x13, 0x88, 0, 0x13}};
    const can_fragment last{0x201, 3, {0xC6, 0x88, 0x6B}};
    return same(left.first, first) && same(left.last, last);
}
static_assert(golden_frames(), "oldframe +500 rpm/current limit golden CAN frames changed");
static_assert(make_string_frames(2, 0, -500.0f, 3000, 5000).first.data[1] == 1);
static_assert(make_string_frames(1, 1, 500.0f, 3000, 5000).first.data[1] == 1);
static_assert(make_string_frames(1, 1, -500.0f, 3000, 5000).first.data[1] == 0);
static_assert(make_string_frames(1, 1, 0.0f, 3000, 5000).first.data[1] == 1);
static_assert(make_string_frames(1, 1, -0.0f, 3000, 5000).first.data[1] == 1);
static_assert(make_string_frames(1, 1, 12.25f, 3000, 5000).first.data[5] == 122);
static_assert(make_string_frames(1, 1, 25.5f, 3000, 5000).first.data[4] == 0);
static_assert(make_string_frames(1, 1, 25.5f, 3000, 5000).first.data[5] == 255);
static_assert(make_string_frames(1, 1, 25.6f, 3000, 5000).first.data[4] == 1);
static_assert(make_string_frames(1, 1, 25.6f, 3000, 5000).first.data[5] == 0);
static_assert(make_string_frames(1, 1, 6553.5f, 3000, 5000).first.data[4] == 255);
static_assert(make_string_frames(1, 1, 6553.5f, 3000, 5000).first.data[5] == 255);
constexpr bool old_packet_equivalence() {
    const float speeds[] = {-5000.0f, -500.0f, -25.6f, -12.25f, -0.0f, 0.0f, 12.25f, 25.5f, 25.6f, 500.0f, 5000.0f};
    for (uint8_t address : {uint8_t(1), uint8_t(2)}) {
        for (uint8_t positive_dir : {uint8_t(0), uint8_t(1)}) {
            for (float rpm : speeds) {
                const uint8_t dir = rpm >= 0 ? positive_dir : (positive_dir == 0 ? 1 : 0);
                const auto velocity = static_cast<std::uint16_t>((rpm >= 0 ? rpm : -rpm) * 10.0f);
                // Raw X_V2_Vel_LC_Control bytes before the original splitter.
                const uint8_t raw[] = {address, 0xC6, dir, 0x0B, 0xB8,
                    uint8_t(velocity >> 8U), uint8_t(velocity), 0, 0x13, 0x88, 0x6B};
                const auto old = legacy_split(raw, sizeof(raw));
                const auto next = make_string_frames(address, positive_dir, rpm, 3000, 5000);
                if (!same(old.first, next.first) || !same(old.last, next.last)) return false;
            }
        }
    }
    return true;
}
static_assert(old_packet_equivalence(), "0xC6 differs from the original classic CAN splitter");
constexpr bool golden_unwrap() {
    position_state positive{}, negative{}, boundary{};
    if (!near(unfold_position(positive, 3.0f), 3.0f)) return false;
    if (!near(unfold_position(positive, -3.0f), 3.2831853f)) return false;
    if (!near(unfold_position(negative, -3.0f), -3.0f)) return false;
    if (!near(unfold_position(negative, 3.0f), -3.2831853f)) return false;
    unfold_position(boundary, 0.0f);
    unfold_position(boundary, math::pi);
    if (boundary.rounds != 0) return false; // Strict > pi, as in oldframe.
    unfold_position(boundary, 0.0f);
    return boundary.rounds == 0;
}
static_assert(golden_unwrap(), "wrap boundary/direction changed");
constexpr bool old_unwrap_equivalence() {
    position_state next{};
    legacy_position old{};
    // Forward and reverse through many turns, including all legacy loading
    // positions. Values are wrapped inputs from the unchanged DM public API.
    for (int sample = 0; sample <= 1200; ++sample) {
        float continuous = sample <= 600 ? sample * -0.1f : (sample - 1200) * 0.1f;
        float raw = continuous;
        while (raw > math::pi) raw -= 2.0f * math::pi;
        while (raw < -math::pi) raw += 2.0f * math::pi;
        const float actual = unfold_position(next, raw);
        if (!near(actual, old.update(raw))) return false;
        if (!near(actual, continuous)) return false;
    }
    return true;
}
static_assert(old_unwrap_equivalence(), "1201-sample multi-turn trajectory differs from oldframe");
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=shutil.which("arm-none-eabi-g++"))
    parser.add_argument("--legacy-ref", default="origin/oldframe")
    args = parser.parse_args()
    if not args.compiler:
        parser.error("arm-none-eabi-g++ is required (or pass --compiler)")
    output = ROOT / "build" / "dart-motor-tests"
    output.mkdir(parents=True, exist_ok=True)
    harness = output / "motor_checks.cpp"
    harness.write_text(
        '#include "motor_ops.hpp"\n#include <initializer_list>\n'
        'using namespace dart::motor::detail;\n' + reference_code(args.legacy_ref) + CHECKS,
        encoding="utf-8",
    )
    command = [args.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
               "-I", str(ROOT / "app/dart/motor"),
               "-I", str(ROOT / "pnx_libs/math/include"), str(harness)]
    subprocess.run(command, cwd=ROOT, check=True)
    revision = subprocess.check_output(["git", "rev-parse", args.legacy_ref], cwd=ROOT, text=True).strip()
    print(f"PASS: ARM C++ constexpr checks against oldframe {revision}")
    print("Validated golden frames, 44 legacy packet comparisons, wrap boundaries, and 1201 multi-turn samples.")
    print("Compile-time verification only; no hardware was accessed.")


if __name__ == "__main__":
    main()
