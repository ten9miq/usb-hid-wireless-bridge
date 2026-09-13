import sys
from pathlib import Path


def read_ihex(path: Path) -> bytes:
    data = {}
    base = 0
    for line in path.read_text(encoding="ascii").splitlines():
        if not line or not line.startswith(":"):
            continue
        raw = bytes.fromhex(line[1:])
        count, address, record_type = raw[0], int.from_bytes(raw[1:3], "big"), raw[3]
        payload = raw[4 : 4 + count]
        if record_type == 0:
            for offset, value in enumerate(payload):
                data[base + address + offset] = value
        elif record_type == 1:
            break
        elif record_type == 4:
            base = int.from_bytes(payload, "big") << 16
    if not data:
        raise ValueError("Intel HEX contains no data records")
    start = min(data)
    end = max(data) + 1
    return bytes(data.get(address, 0xFF) for address in range(start, end))


def main() -> int:
    if len(sys.argv) != 4:
        raise SystemExit("usage: hex_to_c_array.py INPUT.hex OUTPUT.c SYMBOL")
    payload = read_ihex(Path(sys.argv[1]))
    output = Path(sys.argv[2])
    symbol = sys.argv[3]
    lines = [f"const unsigned char {symbol}[] = {{"]
    for index in range(0, len(payload), 16):
        lines.append("    " + ", ".join(f"0x{v:02X}" for v in payload[index : index + 16]) + ",")
    lines.append("};")
    lines.append(f"const unsigned int {symbol}_length = {len(payload)};")
    output.write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")
    header = output.with_suffix(".h")
    header.write_text(
        f"#pragma once\n\nextern const unsigned char {symbol}[];\n"
        f"extern const unsigned int {symbol}_length;\n",
        encoding="ascii",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
