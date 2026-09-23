#!/usr/bin/env python3
"""Gate the temporary queue64 B input-endpoint diagnostic combined UF2."""

import argparse
import hashlib
import importlib.util
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE_B_SHA256 = "7db4b42d476825f6e0296e16b60be66d64e6558ba312e95a4f152fee3c206d12"


def load_module(name: str, filename: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cache_values(path: Path) -> dict[str, str]:
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if ":" in line and "=" in line:
            key, value = line.split("=", 1)
            result[key.split(":", 1)[0]] = value
    return result


def require(values: dict[str, str], expected: dict[str, str]) -> None:
    for key, value in expected.items():
        if values.get(key) != value:
            raise ValueError(f"{key}: expected {value!r}, got {values.get(key)!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--combined", type=Path, required=True)
    parser.add_argument("--a-bin", type=Path, required=True)
    parser.add_argument("--a-cache", type=Path, required=True)
    parser.add_argument("--baseline-b-bin", type=Path, required=True)
    parser.add_argument("--diagnostic-b-hex", type=Path, required=True)
    parser.add_argument("--b-loader-bin", type=Path, required=True)
    parser.add_argument("--b-cache", type=Path, required=True)
    parser.add_argument("--completion-reserve", action="store_true")
    args = parser.parse_args()

    release = load_module("firmware_release_verifier", "verify-firmware-release.py")
    ihex = load_module("hex_to_c_array", "hex_to_c_array.py")
    stable = release.BEST_STABLE_REFERENCE.read_bytes()
    if release.sha256(stable) != release.BEST_STABLE_SHA256:
        raise ValueError("best-stable combined reference hash changed")
    baseline_b = args.baseline_b_bin.read_bytes()
    if len(baseline_b) != 48332 or release.sha256(baseline_b) != BASELINE_B_SHA256:
        raise ValueError("diagnostic-off B rebuild does not match the verified queue64 runtime")

    a_cache = cache_values(args.a_cache)
    b_cache = cache_values(args.b_cache)
    require(a_cache, {
        "PICO_BOARD": "remapper_v7", "CMAKE_BUILD_TYPE": "Release",
        "HID_HOST_DIAGNOSTICS": "ON", "ALT_NUMPAD_EQUALS": "ON",
        "NATIVE_KEYPAD_DEDUP": "OFF", "MOUSE_PIPELINE_TRACE": "OFF",
        "DUAL_A_SERIAL_DRAIN_LIMIT": "1",
    })
    require(b_cache, {
        "PICO_BOARD": "remapper_v7", "CMAKE_BUILD_TYPE": "Release",
        "HID_HOST_DIAGNOSTICS": "ON", "HOST_QUEUE_CAPACITY_64": "ON",
        "HOST_SOF_COALESCE": "OFF", "HOST_ONE_HOT_FAIRNESS": "OFF",
        "VERIFIED_B_BUILD_DATE": "Sep 19 2026", "DUAL_B_BINARY_OVERRIDE": "",
        "HOST_COMPLETION_QUEUE_RESERVE": "ON" if args.completion_reserve else "OFF",
    })
    if Path(b_cache.get("DUAL_B_SOURCE_OVERRIDE", "")).name != "remapper_dual_b_verified_20260919.cc":
        raise ValueError("B application source is not the verified baseline source")
    if Path(b_cache.get("PICO_TINYUSB_PATH", "")).name != "build-g700-clean-tinyusb-src":
        raise ValueError("B TinyUSB source is not the verified baseline source")

    image, ram_blocks = release.parse_combined(args.combined.read_bytes())
    a_bin = args.a_bin.read_bytes()
    if not image.startswith(a_bin) or image.count(release.pinned_embedded_b()) != 1:
        raise ValueError("A flash payload differs from the fresh BIN or pinned embedded B")
    addresses = [struct.unpack_from("<I", ram_blocks, offset + 12)[0]
                 for offset in range(0, len(ram_blocks), 512)]
    if addresses[0] != 0x20000000 or any(
            following != current + 256 for current, following in zip(addresses, addresses[1:])):
        raise ValueError("B RAM loader blocks are not contiguous")
    loader_image = b"".join(ram_blocks[offset + 32:offset + 288]
                            for offset in range(0, len(ram_blocks), 512))
    loader_bin = args.b_loader_bin.read_bytes()
    if not loader_image.startswith(loader_bin):
        raise ValueError("B RAM stage differs from fresh flash_b_side BIN")
    b_runtime = ihex.read_ihex(args.diagnostic_b_hex)
    if loader_bin.count(b_runtime) != 1 or len(b_runtime) < 48000:
        raise ValueError("B RAM loader does not contain exactly one diagnostic B runtime")
    if len(stable) == args.combined.stat().st_size and stable == args.combined.read_bytes():
        raise ValueError("diagnostic candidate is unexpectedly identical to best stable")

    print(f"PASS: diagnostic combined SHA256 {hashlib.sha256(args.combined.read_bytes()).hexdigest()}")
    print("PASS: exact diagnostic-off B baseline; queue64/no SOF coalescing/no fairness")
    print(f"PASS: completion queue reserve {'ON' if args.completion_reserve else 'OFF'}")
    print("PASS: fresh A BIN, pinned embedded B, fresh RAM loader and one diagnostic B runtime")
    print("STATIC ONLY: endpoint reports and mouse/keyboard operation require hardware testing")


if __name__ == "__main__":
    main()
