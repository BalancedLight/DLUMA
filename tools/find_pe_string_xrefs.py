"""Find simple x64 RIP-relative references to an ASCII string in a PE file.

This deliberately has no third-party dependencies. It is an investigation
helper for exact-build Chrome Engine DLLs, not runtime loader code.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pe", type=Path)
    parser.add_argument("text")
    args = parser.parse_args()

    image = args.pe.read_bytes()
    nt = u32(image, 0x3C)
    section_count = u16(image, nt + 6)
    optional_size = u16(image, nt + 20)
    optional = nt + 24
    image_base = struct.unpack_from("<Q", image, optional + 24)[0]
    section_table = optional + optional_size

    sections: list[tuple[str, int, int, int, int]] = []
    for index in range(section_count):
        entry = section_table + index * 40
        name = image[entry : entry + 8].split(b"\0", 1)[0].decode("ascii")
        virtual_size = u32(image, entry + 8)
        virtual_address = u32(image, entry + 12)
        raw_size = u32(image, entry + 16)
        raw_offset = u32(image, entry + 20)
        sections.append((name, virtual_address, virtual_size, raw_offset, raw_size))

    needle = args.text.encode("ascii") + b"\0"
    targets: list[int] = []
    for name, rva, virtual_size, raw, raw_size in sections:
        region = image[raw : raw + raw_size]
        start = 0
        while True:
            found = region.find(needle, start)
            if found < 0:
                break
            target_rva = rva + found
            targets.append(target_rva)
            print(f"string section={name} rva=0x{target_rva:08X} va=0x{image_base + target_rva:016X}")
            start = found + 1

    text_section = next(section for section in sections if section[0] == ".text")
    _, text_rva, _, text_raw, text_raw_size = text_section
    code = image[text_raw : text_raw + text_raw_size]
    for target_rva in targets:
        target_va = image_base + target_rva
        for displacement_offset in range(0, len(code) - 4):
            displacement = struct.unpack_from("<i", code, displacement_offset)[0]
            displacement_end_rva = text_rva + displacement_offset + 4
            if image_base + displacement_end_rva + displacement != target_va:
                continue
            around_start = max(0, displacement_offset - 12)
            around_end = min(len(code), displacement_offset + 20)
            around = code[around_start:around_end].hex(" ").upper()
            print(
                f"xref disp_rva=0x{text_rva + displacement_offset:08X} "
                f"near_rva=0x{text_rva + around_start:08X} bytes={around}"
            )


if __name__ == "__main__":
    main()
