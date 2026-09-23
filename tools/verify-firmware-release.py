#!/usr/bin/env python3
"""Check the verified dual-board UF2 layout and pinned release inputs."""

import argparse
import hashlib
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "firmware/artifacts/remapper_dual_combined-current-features-historical-embedded-b-ab.uf2"
REFERENCE_SHA256 = "c144e09826bb9ebf63c989e08c0eee983a7b5a7acb63e729d3fd62686f05eac2"
EMBEDDED_B_ADDRESS = 0x1002ACD0  # dual_b_binary in the verified A-side image
EMBEDDED_B_LENGTH = 48644
EMBEDDED_B_SHA256 = "ad90682d9b0af74dc5bd8aa47c00874ad69158980d8d4b433e58aa06aa01d4e4"
RUNTIME_B_SHA256 = "03639d06680176ae07e403f29704805b32eaf69f7738ad5337fb8c02c5821c4e"
RUNTIME_B_BINARY_OFFSET = 23352  # B binary within the verified RAM flash loader
RUNTIME_B_BINARY_LENGTH = 48332
RUNTIME_B_BINARY_SHA256 = "9d77cc7378fceb10f692338198db1f4682412a0e48028e81126fbf0c761b8219"
MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
RELEASE_CACHE = {
    "PICO_BOARD": "remapper_v7",
    "CMAKE_BUILD_TYPE": "Release",
    "ALT_NUMPAD_EQUALS": "ON",
    "HID_HOST_DIAGNOSTICS": "OFF",
    "MOUSE_PIPELINE_TRACE": "OFF",
    "LEGACY_RELATIVE_AGGREGATION": "OFF",
    "STABLE_A_C436_BASELINE": "OFF",
    "STABLE_CONSUMER_DESCRIPTOR": "OFF",
    "STABLE_MOUSE_BUTTON_DESCRIPTOR": "OFF",
    "DUAL_A_SERIAL_DRAIN_LIMIT": "1",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def checked_reference() -> bytes:
    data = REFERENCE.read_bytes()
    if sha256(data) != REFERENCE_SHA256:
        raise ValueError(f"verified reference UF2 hash changed: {REFERENCE}")
    return data


def parse_combined(data: bytes) -> tuple[bytes, bytes]:
    if not data or len(data) % 512:
        raise ValueError("UF2 length must be a nonzero multiple of 512")

    flash = {}
    ram_blocks = []
    flash_totals = set()
    ram_totals = set()
    for offset in range(0, len(data), 512):
        block = data[offset : offset + 512]
        if (struct.unpack_from("<II", block) != MAGIC[:2] or
                struct.unpack_from("<I", block, 508)[0] != MAGIC[2]):
            raise ValueError(f"invalid UF2 magic at block {offset // 512}")
        address, payload_size, _block_number, total = struct.unpack_from("<IIII", block, 12)
        if payload_size != 256:
            raise ValueError(f"unexpected payload size at block {offset // 512}")
        if 0x10000000 <= address < 0x11000000:
            if address in flash:
                raise ValueError(f"duplicate A flash address {address:#x}")
            flash[address] = block[32:288]
            flash_totals.add(total)
        elif 0x20000000 <= address < 0x21000000:
            ram_blocks.append(block)
            ram_totals.add(total)
        else:
            raise ValueError(f"unexpected UF2 target address {address:#x}")

    if not flash or not ram_blocks:
        raise ValueError("both A flash and flash_b_side RAM blocks are required")
    if flash_totals != {max(len(flash), len(ram_blocks)) + 1}:
        raise ValueError("A block totals do not use combine_uf2.py layout")
    if ram_totals != {len(ram_blocks)}:
        raise ValueError("B RAM block totals are inconsistent")
    flash_addresses = sorted(flash)
    if flash_addresses[0] != 0x10000000 or any(
            next_address != address + 256
            for address, next_address in zip(flash_addresses, flash_addresses[1:])):
        raise ValueError("A flash blocks are not contiguous from 0x10000000")
    return b"".join(flash[address] for address in flash_addresses), b"".join(ram_blocks)


def pinned_embedded_b() -> bytes:
    image, _ = parse_combined(checked_reference())
    offset = EMBEDDED_B_ADDRESS - 0x10000000
    blob = image[offset : offset + EMBEDDED_B_LENGTH]
    if len(blob) != EMBEDDED_B_LENGTH or sha256(blob) != EMBEDDED_B_SHA256:
        raise ValueError("embedded B in the verified reference is inconsistent")
    return blob


def check_cache(path: Path) -> None:
    values = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    for key, expected in RELEASE_CACHE.items():
        if values.get(key) != expected:
            raise ValueError(f"{key}: expected {expected}, found {values.get(key)!r}")


def hex_record(address: int, kind: int, payload: bytes) -> str:
    body = bytes((len(payload), address >> 8, address & 0xFF, kind)) + payload
    return ":" + (body + bytes(((-sum(body)) & 0xFF,))).hex().upper()


def extract_hex(output: Path) -> None:
    blob = pinned_embedded_b()
    lines = []
    previous_upper = None
    for offset in range(0, len(blob), 16):
        address = EMBEDDED_B_ADDRESS + offset
        upper = address >> 16
        if upper != previous_upper:
            lines.append(hex_record(0, 4, upper.to_bytes(2, "big")))
            previous_upper = upper
        lines.append(hex_record(address & 0xFFFF, 0, blob[offset : offset + 16]))
    lines.append(hex_record(0, 1, b""))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"extracted pinned B HEX: {output} ({len(blob)} bytes, SHA256 {sha256(blob)})")


def extract_b_uf2(output: Path) -> None:
    _, ram_blocks = parse_combined(checked_reference())
    if sha256(ram_blocks) != RUNTIME_B_SHA256:
        raise ValueError("B RAM stage in the verified reference is inconsistent")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(ram_blocks)
    print(f"extracted pinned flash_b_side.uf2: {output} (SHA256 {sha256(ram_blocks)})")


def extract_runtime_b_hex(output: Path) -> None:
    _, ram_blocks = parse_combined(checked_reference())
    if sha256(ram_blocks) != RUNTIME_B_SHA256:
        raise ValueError("B RAM stage in the verified reference is inconsistent")
    payload = b"".join(
        ram_blocks[offset + 32 : offset + 288]
        for offset in range(0, len(ram_blocks), 512)
    )
    blob = payload[RUNTIME_B_BINARY_OFFSET : RUNTIME_B_BINARY_OFFSET + RUNTIME_B_BINARY_LENGTH]
    if len(blob) != RUNTIME_B_BINARY_LENGTH or sha256(blob) != RUNTIME_B_BINARY_SHA256:
        raise ValueError("running B image in the verified reference is inconsistent")
    lines = []
    previous_upper = None
    for offset in range(0, len(blob), 16):
        address = 0x10000000 + offset
        upper = address >> 16
        if upper != previous_upper:
            lines.append(hex_record(0, 4, upper.to_bytes(2, "big")))
            previous_upper = upper
        lines.append(hex_record(address & 0xFFFF, 0, blob[offset : offset + 16]))
    lines.append(hex_record(0, 1, b""))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    print(f"extracted verified running B HEX: {output} "
          f"({len(blob)} bytes, SHA256 {sha256(blob)})")


def extract_a_uf2(output: Path) -> None:
    reference = checked_reference()
    image, _ = parse_combined(reference)
    blocks = []
    saw_ram = False
    for offset in range(0, len(reference), 512):
        block = bytearray(reference[offset : offset + 512])
        address = struct.unpack_from("<I", block, 12)[0]
        if 0x20000000 <= address < 0x21000000:
            saw_ram = True
            continue
        if saw_ram:
            raise ValueError("A flash block appears after B RAM stage")
        blocks.append(block)

    if len(blocks) * 256 != len(image):
        raise ValueError("A flash block count differs from verified image")
    for index, block in enumerate(blocks):
        if struct.unpack_from("<II", block, 12) != (0x10000000 + index * 256, 256):
            raise ValueError(f"unexpected A flash block order at {index}")
        struct.pack_into("<II", block, 20, index, len(blocks))

    uf2 = b"".join(blocks)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(uf2)
    print(f"extracted verified A-only UF2: {output} "
          f"({len(blocks)} blocks, SHA256 {sha256(uf2)})")


def verify(combined: Path, a_bin: Path, cache: Path) -> None:
    golden_b = pinned_embedded_b()
    check_cache(cache)
    candidate = combined.read_bytes()
    image, ram_blocks = parse_combined(candidate)
    binary = a_bin.read_bytes()
    if not image.startswith(binary):
        raise ValueError("A UF2 payload does not match the freshly built remapper_dual_a.bin")
    if image.count(golden_b) != 1:
        raise ValueError("A image does not contain exactly one pinned embedded B binary")
    if sha256(ram_blocks) != RUNTIME_B_SHA256:
        raise ValueError("B RAM stage differs from verified flash_b_side.uf2")
    print(f"PASS: combined SHA256 {sha256(candidate)}")
    print(f"PASS: A BIN prefix ({len(binary)} bytes), pinned embedded B, B RAM stage, UF2 block layout")
    print("Hardware move-stop and keyboard checks are still required before release.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    extract = sub.add_parser("extract-b-hex", help="recreate the pinned B HEX from the verified UF2")
    extract.add_argument("output", type=Path)
    extract_ram = sub.add_parser("extract-b-uf2", help="recreate the pinned flash_b_side.uf2")
    extract_ram.add_argument("output", type=Path)
    extract_running_b = sub.add_parser("extract-runtime-b-hex", help="recreate the verified running B HEX")
    extract_running_b.add_argument("output", type=Path)
    extract_a = sub.add_parser("extract-a-uf2", help="recreate the verified A-only UF2")
    extract_a.add_argument("output", type=Path)
    check = sub.add_parser("verify", help="verify a candidate combined UF2 before flashing")
    check.add_argument("--combined", required=True, type=Path)
    check.add_argument("--a-bin", required=True, type=Path)
    check.add_argument("--cmake-cache", required=True, type=Path)
    args = parser.parse_args()
    if args.command == "extract-b-hex":
        extract_hex(args.output)
    elif args.command == "extract-b-uf2":
        extract_b_uf2(args.output)
    elif args.command == "extract-runtime-b-hex":
        extract_runtime_b_hex(args.output)
    elif args.command == "extract-a-uf2":
        extract_a_uf2(args.output)
    else:
        verify(args.combined, args.a_bin, args.cmake_cache)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
