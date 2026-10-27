#!/usr/bin/env python3
"""Decode the deterministic validation BIN emitted by compile_localized_scr."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from compile_localized_scr import MAGIC, ScrError


def decompile_values(data: bytes) -> dict[str, str]:
    if not data.startswith(MAGIC) or len(data) < len(MAGIC) + 4:
        raise ScrError("not a DLUMA localization BIN")
    offset = len(MAGIC)
    count = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    values: dict[str, str] = {}
    for _ in range(count):
        if offset + 2 > len(data):
            raise ScrError("truncated key length")
        key_length = struct.unpack_from("<H", data, offset)[0]
        offset += 2
        if offset + key_length + 4 > len(data):
            raise ScrError("truncated key")
        key = data[offset:offset + key_length].decode("ascii")
        offset += key_length
        value_units = struct.unpack_from("<I", data, offset)[0]
        offset += 4
        value_bytes = value_units * 2
        if offset + value_bytes > len(data):
            raise ScrError("truncated UTF-16LE value")
        value = data[offset:offset + value_bytes].decode("utf-16le")
        offset += value_bytes
        if key in values:
            raise ScrError(f"duplicate key {key}")
        values[key] = value
    if offset != len(data):
        raise ScrError("trailing bytes in localization BIN")
    return values


def decompile_file(source: Path, output: Path | None = None) -> dict[str, str]:
    values = decompile_values(source.read_bytes())
    if output is not None:
        lines = ["!String(s, s)", ""]
        for key in sorted(values):
            value = (values[key].replace("\\", "\\\\")
                     .replace('"', '\\"').replace("\r", "\\r")
                     .replace("\n", "\\n").replace("\t", "\\t"))
            lines.append(f'String("{key}", "{value}")')
        output.write_text("\r\n".join(lines) + "\r\n", encoding="utf-8", newline="")
    return values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    decompile_file(args.source, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
