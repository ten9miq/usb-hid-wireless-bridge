#!/usr/bin/env python3
"""Build and statically gate the once-per-boot NumLock initialization trial."""

import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware/hid-remapper/firmware"
BUILD = FW / "build-startup-numlock-sync"
OUTPUT = ROOT / "firmware/artifacts/remapper_dual_combined-startup-numlock-sync-test.uf2"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args):
    print("RUN:", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True)


def main():
    spec = importlib.util.spec_from_file_location("release", ROOT / "tools/verify-firmware-release.py")
    release = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(release)
    reference = release.checked_keypad_reserve()
    reference_a, reference_b = release.parse_combined(reference)
    # Keep the reference's embedded __DATE__ metadata for byte-identical
    # baseline reproduction. Record the actual build time in the manifest.
    source_epoch = int(datetime.datetime(2026, 9, 23, tzinfo=datetime.timezone.utc).timestamp())
    os.environ["SOURCE_DATE_EPOCH"] = str(source_epoch)

    stamp = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=9))).strftime("%Y%m%d_%H%M%S")
    backup = ROOT / "backups" / f"backups_{stamp}"
    backup.mkdir(parents=True, exist_ok=False)
    BUILD.mkdir(parents=True, exist_ok=True)
    if OUTPUT.exists():
        shutil.copy2(OUTPUT, backup / OUTPUT.name)
    # These historical source overrides reproduce the verified A application.
    # Keep the exact source inputs in a timestamped backup for reproducibility.
    historical = ROOT / "tools/firmware-baselines"
    for name in ("verified-a-source.cc", "verified-a-flash.c"):
        shutil.copy2(historical / name, backup / name)
    for source in (FW / "src/remapper.cc", FW / "src/startup_numlock_sync.h", FW / "CMakeLists.txt"):
        shutil.copy2(source, backup / source.name)

    gate = ROOT / "tools/verify-firmware-release.py"
    run(sys.executable, gate, "extract-b-hex", BUILD / "pinned-embedded-b.hex")
    run(sys.executable, gate, "extract-b-uf2", BUILD / "flash_b_side.uf2", "--keypad-completion-reserve")
    configure = [
        "cmake", "-S", FW, "-B", BUILD, "-G", "Ninja",
        f"-DPython3_EXECUTABLE={sys.executable}",
        # CMake 4.4 omits config-specific flags on some SDK interface sources
        # in a fresh build. Pin the verified Release optimization for all C/C++.
        "-DCMAKE_C_FLAGS=-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -ftls-model=local-exec -g -O3 -DNDEBUG",
        "-DCMAKE_CXX_FLAGS=-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -ftls-model=local-exec -g -O3 -DNDEBUG",
        "-DCMAKE_C_COMPILER=C:/ProgramData/chocolatey/lib/gcc-arm-embedded/tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-gcc.exe",
        "-DCMAKE_CXX_COMPILER=C:/ProgramData/chocolatey/lib/gcc-arm-embedded/tools/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-g++.exe",
        "-DPICO_BOARD=remapper_v7", "-DCMAKE_BUILD_TYPE=Release", "-DPICO_NO_PICOTOOL=ON",
        "-DALT_NUMPAD_EQUALS=ON", "-DNATIVE_KEYPAD_DEDUP=ON",
        "-DWBT2_NUMERIC_KEYPAD_PRIORITY=OFF", "-DWBT2_SUPPRESS_23UB_NUM_LOCK=OFF",
        "-DHID_HOST_DIAGNOSTICS=OFF", "-DMOUSE_PIPELINE_TRACE=OFF",
        "-DSTABLE_A_C436_BASELINE=OFF", "-DSTABLE_CONSUMER_DESCRIPTOR=OFF",
        "-DSTABLE_MOUSE_BUTTON_DESCRIPTOR=OFF", "-DLEGACY_RELATIVE_AGGREGATION=OFF",
        "-DDUAL_A_SERIAL_DRAIN_LIMIT=1",
        f"-DDUAL_A_SOURCE_OVERRIDE={backup / 'verified-a-source.cc'}",
        f"-DDUAL_A_FLASH_SOURCE_OVERRIDE={backup / 'verified-a-flash.c'}",
        f"-DDUAL_B_BINARY_OVERRIDE={BUILD / 'pinned-embedded-b.hex'}",
    ]
    run(*configure, "-DSTARTUP_NUMLOCK_SYNC=OFF")
    # Ninja does not track environment changes; rebuild the date-bearing SDK
    # object if this build directory already existed before SOURCE_DATE_EPOCH.
    (BUILD / "CMakeFiles/remapper_dual_a.dir/pico-sdk/src/rp2_common/pico_standard_binary_info/standard_binary_info.c.obj").unlink(missing_ok=True)
    run("cmake", "--build", BUILD, "--target", "remapper_dual_a", "--parallel", "4")
    baseline = (BUILD / "remapper_dual_a.bin").read_bytes()
    if baseline.ljust((len(baseline) + 255) // 256 * 256, b"\0") != reference_a:
        raise ValueError("startup-OFF rebuild does not exactly reproduce the verified A")
    shutil.copy2(BUILD / "remapper_dual_a.bin", backup / "startup-off-a.bin")
    object_root = BUILD / "CMakeFiles/remapper_dual_a.dir"
    objects_off = {str(p.relative_to(object_root)): digest(p) for p in object_root.rglob("*.obj")}
    print("PASS: startup-OFF A matches verified keypad/completion-reserve reference byte for byte", flush=True)

    run(*configure, "-DSTARTUP_NUMLOCK_SYNC=ON")
    run("cmake", "--build", BUILD, "--target", "remapper_dual_a", "--parallel", "4")
    objects_on = {str(p.relative_to(object_root)): digest(p) for p in object_root.rglob("*.obj")}
    changed = [name for name in objects_off if objects_off[name] != objects_on[name]]
    if changed != [str(Path("src/remapper.cc.obj"))]:
        raise ValueError(f"unexpected changed A objects: {changed}")
    run(ROOT / "tools/picotool/picotool.exe", "uf2", "convert",
        BUILD / "remapper_dual_a.elf", BUILD / "remapper_dual_a.uf2", "--family", "rp2040")
    run(sys.executable, FW / "combine_uf2.py", BUILD / "remapper_dual_a.uf2",
        BUILD / "flash_b_side.uf2", OUTPUT)
    run(sys.executable, gate, "verify", "--combined", OUTPUT,
        "--a-bin", BUILD / "remapper_dual_a.bin", "--cmake-cache", BUILD / "CMakeCache.txt",
        "--native-keypad-dedup", "--startup-numlock-sync")
    actual_a, actual_b = release.parse_combined(OUTPUT.read_bytes())
    if actual_b != reference_b:
        raise ValueError("runtime B differs from verified keypad/completion-reserve reference")

    run("g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        ROOT / "tools/startup-numlock-sync-test.cc", "-o", BUILD / "startup-numlock-sync-test.exe")
    run(BUILD / "startup-numlock-sync-test.exe")
    manifest = {
        "created_jst": datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=9))).isoformat(),
        "reference": str(release.KEYPAD_RESERVE_REFERENCE.relative_to(ROOT)),
        "reference_sha256": release.KEYPAD_RESERVE_SHA256,
        "combined": str(OUTPUT.relative_to(ROOT)), "combined_sha256": digest(OUTPUT),
        "baseline_off_a_sha256": hashlib.sha256(baseline).hexdigest(),
        "a_bin_sha256": digest(BUILD / "remapper_dual_a.bin"),
        "a_flash_sha256": hashlib.sha256(actual_a).hexdigest(),
        "runtime_b_uf2_sha256": hashlib.sha256(actual_b).hexdigest(),
        "embedded_b_sha256": release.EMBEDDED_B_SHA256,
        "source_date_epoch": source_epoch,
        "changed_a_objects": changed,
        "source_backup": str(backup.relative_to(ROOT)),
        "historical_source_hashes": {name: digest(backup / name) for name in ("verified-a-source.cc", "verified-a-flash.c")},
        "configure_arguments": list(map(str, configure)) + ["-DSTARTUP_NUMLOCK_SYNC=ON"],
        "static_checks": "PASS", "hardware_tests": "NOT_RUN", "flashed": False,
    }
    manifest_path = OUTPUT.with_suffix(".json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(manifest_path, backup / manifest_path.name)
    shutil.copy2(BUILD / "CMakeCache.txt", backup / "CMakeCache.txt")
    print(f"PASS: candidate {OUTPUT}\nSHA256 {digest(OUTPUT)}\nSTATIC ONLY: no flashing or hardware verification")


if __name__ == "__main__":
    main()
