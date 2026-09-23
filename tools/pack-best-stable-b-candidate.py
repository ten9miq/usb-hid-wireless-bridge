#!/usr/bin/env python3
"""Pack a freshly built A BIN with the pinned best-stable B UF2 stage."""

import argparse
import hashlib
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BEST = ROOT / "firmware/artifacts/remapper_dual_combined-verified-a-g700-queue64-only-candidate.uf2"
BEST_SHA256 = "8bc16d3d69b0ecd2149ac03a50e4256eb67357439fb081ce4def09e0ad6f6dd3"
MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a-bin", required=True, type=Path)
    parser.add_argument("--b-bin", type=Path,
                        help="fresh RAM flash_b_side BIN; omit to reuse best-stable B")
    parser.add_argument("--a-uf2", required=True, type=Path)
    parser.add_argument("--b-uf2", required=True, type=Path)
    args = parser.parse_args()

    reference = BEST.read_bytes()
    if hashlib.sha256(reference).hexdigest() != BEST_SHA256:
        raise ValueError("best-stable reference hash differs")
    if len(reference) % 512:
        raise ValueError("reference is not UF2 block aligned")

    template = None
    b_template = None
    b_blocks = []
    for offset in range(0, len(reference), 512):
        block = reference[offset:offset + 512]
        address = struct.unpack_from("<I", block, 12)[0]
        if struct.unpack_from("<II", block) != MAGIC[:2] or struct.unpack_from("<I", block, 508)[0] != MAGIC[2]:
            raise ValueError("invalid reference UF2 magic")
        if 0x10000000 <= address < 0x11000000:
            if template is None:
                template = block
        elif 0x20000000 <= address < 0x21000000:
            if b_template is None:
                b_template = block
            b_blocks.append(block)
        else:
            raise ValueError(f"unexpected reference address {address:#x}")
    if template is None or not b_blocks:
        raise ValueError("reference lacks A or B stage")

    binary = args.a_bin.read_bytes()
    if not binary or len(binary) > 0x200000:
        raise ValueError("unexpected A BIN length")
    count = (len(binary) + 255) // 256
    a_blocks = []
    for index in range(count):
        block = bytearray(template)
        struct.pack_into("<IIII", block, 12, 0x10000000 + index * 256, 256, index, count)
        block[32:288] = binary[index * 256:(index + 1) * 256].ljust(256, b"\x00")
        a_blocks.append(block)

    if args.b_bin is not None:
        b_binary = args.b_bin.read_bytes()
        if not b_binary or len(b_binary) > 0x40000 or b_template is None:
            raise ValueError("unexpected B RAM-loader BIN length")
        b_count = (len(b_binary) + 255) // 256
        b_blocks = []
        for index in range(b_count):
            block = bytearray(b_template)
            struct.pack_into("<IIII", block, 12,
                             0x20000000 + index * 256, 256, index, b_count)
            block[32:288] = b_binary[index * 256:(index + 1) * 256].ljust(256, b"\x00")
            b_blocks.append(block)

    args.a_uf2.parent.mkdir(parents=True, exist_ok=True)
    args.b_uf2.parent.mkdir(parents=True, exist_ok=True)
    args.a_uf2.write_bytes(b"".join(a_blocks))
    args.b_uf2.write_bytes(b"".join(b_blocks))
    print(f"A UF2: {len(a_blocks)} flash blocks from fresh BIN")
    b_source = "fresh RAM-loader BIN" if args.b_bin is not None else "pinned best-stable"
    print(f"B UF2: {len(b_blocks)} {b_source} RAM blocks")


if __name__ == "__main__":
    main()
