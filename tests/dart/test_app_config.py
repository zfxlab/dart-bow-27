"""Exercise the real CMake generator; inputs and outputs stay in build/."""
from pathlib import Path
import copy
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "dart-config-test"
BASE = json.loads((ROOT / "configs/boards/h723_mc02/params.json").read_text(encoding="utf-8-sig"))


def configure(name, config, error=None):
    source = OUT / f"{name}.json"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text(json.dumps(config), encoding="utf-8")
    result = subprocess.run([
        "cmake", "-S", str(ROOT), "-B", str(OUT / name), "-G", "Ninja",
        "-DPNX_BOARD=h723_mc02", "-DCMAKE_BUILD_TYPE=Debug",
        f"-DPNX_PARAMS_OVERRIDE={source.as_posix()}",
    ], capture_output=True, text=True)
    log = result.stdout + result.stderr
    (OUT / f"{name}.log").write_text(log, encoding="utf-8")
    if error:
        assert result.returncode != 0 and error in log, log
    else:
        assert result.returncode == 0, log
    print("PASS:", name)


if __name__ == "__main__":
    configure("enabled", BASE)
    generated = OUT / "enabled/generated/dart_config.hpp"
    text = generated.read_text(encoding="utf-8")
    assert "syn_kp = 10.0f" in text and "trigger_open_us = 1750" in text
    assert "string_l_positive_dir = 0" in text and "string_r_positive_dir = 1" in text
    assert "sequence[4] = {3, 4, 5, 8," in text
    timestamp = generated.stat().st_mtime_ns
    configure("enabled", BASE)
    assert generated.stat().st_mtime_ns == timestamp, "unchanged app constants were rewritten"

    off = copy.deepcopy(BASE)
    off["dart"]["enabled"] = False
    configure("disabled", off)
    manifest = (OUT / "disabled/pnx_embedded_sources.txt").read_text(encoding="utf-8")
    assert "/app/dart/" not in manifest
    assert not (OUT / "disabled/generated/dart_config.hpp").exists()

    no_usb = copy.deepcopy(BASE)
    no_usb["build"]["usbx"] = False
    configure("missing_usb", no_usb, "requires build.usbx=true")
    no_rc = copy.deepcopy(BASE)
    no_rc["remoter"]["source"] = "none"
    configure("missing_remote", no_rc, "remoter.source=dr16")
    zero_period = copy.deepcopy(BASE)
    zero_period["dart"]["motor"]["period_ticks"] = 0
    configure("zero_period", zero_period, "outside the ThreadX range")
    bad_sequence = copy.deepcopy(BASE)
    bad_sequence["dart"]["sequence"][0] = 17
    configure("bad_sequence", bad_sequence, "dart.sequence ID must be in 1..16")
    print("App config checks passed; no source JSON was changed and no hardware was accessed.")
