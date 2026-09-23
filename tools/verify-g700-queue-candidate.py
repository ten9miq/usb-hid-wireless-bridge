#!/usr/bin/env python3
"""Check the isolated G700s queue-depth trial against the verified A/B baseline."""

import argparse
import hashlib
import importlib.util
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERIFIER = ROOT / "tools/verify-firmware-release.py"
EXPECTED_B_SHA256 = "7db4b42d476825f6e0296e16b60be66d64e6558ba312e95a4f152fee3c206d12"
EXPECTED_B_SOURCE = "remapper_dual_b_verified_20260919.cc"


def load_release_verifier():
    spec = importlib.util.spec_from_file_location("firmware_release_verifier", VERIFIER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cache_values(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def require_cache(values: dict[str, str], expected: dict[str, str]) -> None:
    for key, value in expected.items():
        if values.get(key) != value:
            raise ValueError(f"{key}: expected {value!r}, found {values.get(key)!r}")


def verify(combined: Path, a_bin: Path, a_cache: Path,
           b_bin: Path, b_cache: Path) -> None:
    release = load_release_verifier()
    release.check_cache(a_cache)
    require_cache(cache_values(b_cache), {
        "PICO_BOARD": "remapper_v7",
        "CMAKE_BUILD_TYPE": "Release",
        "HID_HOST_DIAGNOSTICS": "OFF",
        "MOUSE_PIPELINE_TRACE": "OFF",
        "HOST_QUEUE_CAPACITY_64": "ON",
        "HOST_SOF_COALESCE": "OFF",
        "HOST_ONE_HOT_FAIRNESS": "OFF",
        "VERIFIED_B_BUILD_DATE": "Sep 19 2026",
        "DUAL_B_BINARY_OVERRIDE": "",
    })
    b_source = cache_values(b_cache).get("DUAL_B_SOURCE_OVERRIDE", "")
    if Path(b_source).name != EXPECTED_B_SOURCE:
        raise ValueError(f"unexpected B application source: {b_source!r}")

    candidate = combined.read_bytes()
    a_image, ram_blocks = release.parse_combined(candidate)
    verified_a, _ = release.parse_combined(release.checked_reference())
    if a_image != verified_a:
        raise ValueError("A flash payload differs from the verified release")
    a_binary = a_bin.read_bytes()
    if not a_image.startswith(a_binary):
        raise ValueError("A BIN does not match the verified A flash payload")
    if a_image.count(release.pinned_embedded_b()) != 1:
        raise ValueError("A embedded B differs from the verified release")

    b_binary = b_bin.read_bytes()
    if len(b_binary) != 48332 or release.sha256(b_binary) != EXPECTED_B_SHA256:
        raise ValueError("B runtime binary is not the isolated queue-depth candidate")
    addresses = [struct.unpack_from("<I", ram_blocks, offset + 12)[0]
                 for offset in range(0, len(ram_blocks), 512)]
    if addresses[0] != 0x20000000 or any(
            following != current + 256
            for current, following in zip(addresses, addresses[1:])):
        raise ValueError("B RAM-loader UF2 addresses are not contiguous")
    loader_payload = b"".join(
        ram_blocks[offset + 32:offset + 288]
        for offset in range(0, len(ram_blocks), 512)
    )
    if loader_payload.count(b_binary) != 1:
        raise ValueError("B RAM loader does not contain exactly one candidate runtime")

    digest = hashlib.sha256(candidate).hexdigest()
    print(f"PASS: candidate combined SHA256 {digest}")
    print("PASS: verified A unchanged; B runtime queue64-only payload; UF2 RAM layout")
    print("STATIC ONLY: hardware results and remaining key checks are recorded separately")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--combined", required=True, type=Path)
    parser.add_argument("--a-bin", required=True, type=Path)
    parser.add_argument("--a-cache", required=True, type=Path)
    parser.add_argument("--b-bin", required=True, type=Path)
    parser.add_argument("--b-cache", required=True, type=Path)
    args = parser.parse_args()
    verify(args.combined, args.a_bin, args.a_cache, args.b_bin, args.b_cache)


if __name__ == "__main__":
    main()
