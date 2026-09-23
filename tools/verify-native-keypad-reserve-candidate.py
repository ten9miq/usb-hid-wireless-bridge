#!/usr/bin/env python3
"""Check the combined native-keypad dedup A and completion-reserve B trial."""

import argparse
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEDUP = ROOT / "firmware/artifacts/remapper_dual_combined-best-stable-native-keypad-dedup-test.uf2"
DEDUP_SHA256 = "c6adcb9bd12a4aa6d8fb315dd6e301ba56416ba0a0a49f1651e01739d94190ab"
RESERVE = ROOT / "firmware/artifacts/remapper_dual_combined-best-stable-completion-queue-reserve-production-test.uf2"
RESERVE_SHA256 = "9383b54b97038bd8e66dcf114a6e895fa2fb6f47cbaea5fb5cd1beff08b91231"


def load_release():
    spec = importlib.util.spec_from_file_location("release", ROOT / "tools/verify-firmware-release.py")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def cache_values(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--combined", type=Path, required=True)
    parser.add_argument("--a-bin", type=Path, required=True)
    parser.add_argument("--a-cache", type=Path, required=True)
    parser.add_argument("--b-cache", type=Path, required=True)
    args = parser.parse_args()

    release = load_release()
    dedup = DEDUP.read_bytes()
    reserve = RESERVE.read_bytes()
    if release.sha256(dedup) != DEDUP_SHA256 or release.sha256(reserve) != RESERVE_SHA256:
        raise ValueError("component reference UF2 hash changed")
    expected_a, _ = release.parse_combined(dedup)
    _, expected_b = release.parse_combined(reserve)
    actual_a, actual_b = release.parse_combined(args.combined.read_bytes())
    if actual_a != expected_a or actual_b != expected_b:
        raise ValueError("combined A/B stages differ from verified component candidates")
    if not actual_a.startswith(args.a_bin.read_bytes()) or actual_a.count(release.pinned_embedded_b()) != 1:
        raise ValueError("fresh A BIN or pinned embedded B differs")
    a = cache_values(args.a_cache)
    b = cache_values(args.b_cache)
    for key, value in {
        "ALT_NUMPAD_EQUALS": "ON", "NATIVE_KEYPAD_DEDUP": "ON",
        "HID_HOST_DIAGNOSTICS": "OFF", "WBT2_NUMERIC_KEYPAD_PRIORITY": "OFF",
        "WBT2_SUPPRESS_23UB_NUM_LOCK": "OFF", "MOUSE_PIPELINE_TRACE": "OFF",
    }.items():
        if a.get(key) != value:
            raise ValueError(f"A {key} is {a.get(key)!r}, expected {value!r}")
    for key, value in {
        "HID_HOST_DIAGNOSTICS": "OFF", "HOST_QUEUE_CAPACITY_64": "ON",
        "HOST_COMPLETION_QUEUE_RESERVE": "ON", "HOST_SOF_COALESCE": "OFF",
        "HOST_ONE_HOT_FAIRNESS": "OFF", "MOUSE_PIPELINE_TRACE": "OFF",
    }.items():
        if b.get(key) != value:
            raise ValueError(f"B {key} is {b.get(key)!r}, expected {value!r}")

    print(f"PASS: combined trial SHA256 {release.sha256(args.combined.read_bytes())}")
    print("PASS: exact dedup A and queue-reserve B components; fresh A BIN and pinned embedded B")
    print("STATIC ONLY: keypad, keyboard/G700s cold-start and RollerMouse checks remain")


if __name__ == "__main__":
    main()
