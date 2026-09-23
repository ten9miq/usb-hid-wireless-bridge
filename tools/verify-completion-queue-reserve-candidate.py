#!/usr/bin/env python3
"""Check the production completion-reserve trial against verified A and B."""

import argparse
import importlib.util
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE_B_SHA256 = "7db4b42d476825f6e0296e16b60be66d64e6558ba312e95a4f152fee3c206d12"
VERIFIED_A_BIN_SHA256 = "7d86008cef6296c456d02816f347cca72c6d1002ed01a4e1114b12e69d768c59"


def module(name: str, path: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def cache(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def require(values: dict[str, str], expected: dict[str, str]) -> None:
    for key, value in expected.items():
        if values.get(key) != value:
            raise ValueError(f"{key}: expected {value!r}, got {values.get(key)!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--combined", type=Path, required=True)
    parser.add_argument("--a-bin", type=Path, required=True)
    parser.add_argument("--baseline-b-bin", type=Path, required=True)
    parser.add_argument("--b-hex", type=Path, required=True)
    parser.add_argument("--b-loader-bin", type=Path, required=True)
    parser.add_argument("--b-cache", type=Path, required=True)
    args = parser.parse_args()

    release = module("release", "verify-firmware-release.py")
    ihex = module("ihex", "hex_to_c_array.py")
    reference = release.BEST_STABLE_REFERENCE.read_bytes()
    if release.sha256(reference) != release.BEST_STABLE_SHA256:
        raise ValueError("best-stable reference hash changed")
    stable_a, stable_ram = release.parse_combined(reference)
    baseline_b = args.baseline_b_bin.read_bytes()
    if len(baseline_b) != 48332 or release.sha256(baseline_b) != BASELINE_B_SHA256:
        raise ValueError("completion-reserve OFF does not reproduce verified B")
    a_bin = args.a_bin.read_bytes()
    if release.sha256(a_bin) != VERIFIED_A_BIN_SHA256 or not stable_a.startswith(a_bin):
        raise ValueError("A BIN is not the verified stable A build")

    values = cache(args.b_cache)
    require(values, {
        "PICO_BOARD": "remapper_v7", "CMAKE_BUILD_TYPE": "Release",
        "HID_HOST_DIAGNOSTICS": "OFF", "HOST_QUEUE_CAPACITY_64": "ON",
        "HOST_COMPLETION_QUEUE_RESERVE": "ON", "HOST_SOF_COALESCE": "OFF",
        "HOST_ONE_HOT_FAIRNESS": "OFF", "MOUSE_PIPELINE_TRACE": "OFF",
        "VERIFIED_B_BUILD_DATE": "Sep 19 2026", "DUAL_B_BINARY_OVERRIDE": "",
    })
    if Path(values.get("DUAL_B_SOURCE_OVERRIDE", "")).name != "remapper_dual_b_verified_20260919.cc":
        raise ValueError("wrong B application source")
    if Path(values.get("PICO_TINYUSB_PATH", "")).name != "build-g700-clean-tinyusb-src":
        raise ValueError("wrong B TinyUSB source")

    candidate = args.combined.read_bytes()
    a_image, b_blocks = release.parse_combined(candidate)
    if a_image != stable_a or a_image.count(release.pinned_embedded_b()) != 1:
        raise ValueError("candidate A differs from exact best-stable A")
    if b_blocks == stable_ram:
        raise ValueError("candidate B unexpectedly equals best-stable B")
    addresses = [struct.unpack_from("<I", b_blocks, offset + 12)[0]
                 for offset in range(0, len(b_blocks), 512)]
    if addresses[0] != 0x20000000 or any(
            following != current + 256 for current, following in zip(addresses, addresses[1:])):
        raise ValueError("B RAM UF2 addresses are not contiguous")
    loader = b"".join(b_blocks[offset + 32:offset + 288]
                      for offset in range(0, len(b_blocks), 512))
    loader_bin = args.b_loader_bin.read_bytes()
    runtime = ihex.read_ihex(args.b_hex)
    if not loader.startswith(loader_bin) or loader_bin.count(runtime) != 1:
        raise ValueError("fresh B RAM loader or embedded runtime differs")

    print(f"PASS: production trial SHA256 {release.sha256(candidate)}")
    print("PASS: exact best-stable A and pinned embedded B unchanged")
    print("PASS: reserve-OFF B equals verified runtime; reserve-ON B loader and UF2 layout")
    print("STATIC ONLY: repeated cold-start and RollerMouse move-stop checks are still required")


if __name__ == "__main__":
    main()
