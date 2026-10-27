"""Locate x64 MSVC RTTI vftables for one class in a PE image.

This is an offline reverse-engineering helper. It follows the image-relative
CompleteObjectLocator records emitted by the x64 MSVC ABI and prints the first
contiguous code pointers in every matching vftable.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pe", type=Path)
    parser.add_argument("class_name")
    parser.add_argument("--entries", type=int, default=512)
    parser.add_argument("--min-stack", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--object-offset", type=lambda value: int(value, 0))
    args = parser.parse_args()

    image = args.pe.read_bytes()
    nt = u32(image, 0x3C)
    section_count = u16(image, nt + 6)
    optional_size = u16(image, nt + 20)
    optional = nt + 24
    image_base = u64(image, optional + 24)
    section_table = optional + optional_size

    sections: list[tuple[str, int, int, int, int]] = []
    for index in range(section_count):
        entry = section_table + index * 40
        name = image[entry : entry + 8].split(b"\0", 1)[0].decode("ascii")
        sections.append(
            (
                name,
                u32(image, entry + 12),
                u32(image, entry + 8),
                u32(image, entry + 20),
                u32(image, entry + 16),
            )
        )

    def rva_to_raw(rva: int) -> int | None:
        for _, virtual, virtual_size, raw, raw_size in sections:
            if virtual <= rva < virtual + max(virtual_size, raw_size):
                delta = rva - virtual
                return raw + delta if delta < raw_size else None
        return None

    def find_all(needle: bytes) -> list[int]:
        result: list[int] = []
        start = 0
        while True:
            found = image.find(needle, start)
            if found < 0:
                return result
            result.append(found)
            start = found + 1

    def raw_to_rva(raw_offset: int) -> int | None:
        for _, virtual, _, raw, raw_size in sections:
            if raw <= raw_offset < raw + raw_size:
                return virtual + raw_offset - raw
        return None

    imports: dict[int, str] = {}
    import_directory_rva = u32(image, optional + 120)
    import_raw = rva_to_raw(import_directory_rva)
    if import_raw is not None:
        descriptor_raw = import_raw
        while u32(image, descriptor_raw) or u32(image, descriptor_raw + 12):
            original_thunk_rva = u32(image, descriptor_raw)
            name_rva = u32(image, descriptor_raw + 12)
            first_thunk_rva = u32(image, descriptor_raw + 16)
            name_raw = rva_to_raw(name_rva)
            dll_name = "<unknown>"
            if name_raw is not None:
                dll_name = image[name_raw : image.index(b"\0", name_raw)].decode("ascii")
            lookup_raw = rva_to_raw(original_thunk_rva or first_thunk_rva)
            if lookup_raw is not None:
                index = 0
                while True:
                    thunk = u64(image, lookup_raw + index * 8)
                    if not thunk:
                        break
                    if thunk & (1 << 63):
                        symbol = f"ordinal_{thunk & 0xFFFF}"
                    else:
                        hint_name_raw = rva_to_raw(thunk & 0x7FFFFFFF_FFFFFFFF)
                        if hint_name_raw is None:
                            break
                        start = hint_name_raw + 2
                        symbol = image[start : image.index(b"\0", start)].decode("ascii")
                    imports[first_thunk_rva + index * 8] = f"{dll_name}!{symbol}"
                    index += 1
            descriptor_raw += 20

    decorated = f".?AV{args.class_name}@@".encode("ascii") + b"\0"
    text = next(section for section in sections if section[0] == ".text")
    text_start = image_base + text[1]
    text_end = text_start + max(text[2], text[4])

    for name_raw in find_all(decorated):
        name_rva = raw_to_rva(name_raw)
        if name_rva is None or name_rva < 16:
            continue
        type_descriptor_rva = name_rva - 16
        print(f"type_descriptor_rva=0x{type_descriptor_rva:08X}")
        for type_ref_raw in find_all(struct.pack("<I", type_descriptor_rva)):
            col_raw = type_ref_raw - 12
            col_rva = raw_to_rva(col_raw)
            if col_raw < 0 or col_rva is None or col_raw + 24 > len(image):
                continue
            if u32(image, col_raw) != 1 or u32(image, col_raw + 20) != col_rva:
                continue
            col_va = image_base + col_rva
            object_offset = u32(image, col_raw + 4)
            if args.object_offset is not None and object_offset != args.object_offset:
                continue
            for col_ptr_raw in find_all(struct.pack("<Q", col_va)):
                pointer_rva = raw_to_rva(col_ptr_raw)
                if pointer_rva is None:
                    continue
                vftable_rva = pointer_rva + 8
                print(
                    f"vftable_rva=0x{vftable_rva:08X} "
                    f"object_offset=0x{object_offset:X} col_rva=0x{col_rva:08X}"
                )
                table_raw = col_ptr_raw + 8
                for slot in range(args.entries):
                    entry_raw = table_raw + slot * 8
                    if entry_raw + 8 > len(image):
                        break
                    target = u64(image, entry_raw)
                    if not (text_start <= target < text_end):
                        break
                    target_rva = target - image_base
                    target_raw = rva_to_raw(target_rva)
                    preview = ""
                    function_bytes = b""
                    if target_raw is not None:
                        function_bytes = image[target_raw : target_raw + 64]
                        preview = function_bytes[:16].hex(" ").upper()
                    if args.min_stack:
                        allocations = [
                            u32(function_bytes, index + 3)
                            for index in range(max(0, len(function_bytes) - 7))
                            if function_bytes[index : index + 3] == b"\x48\x81\xEC"
                        ]
                        if not allocations or max(allocations) < args.min_stack:
                            continue
                    imported = ""
                    if len(function_bytes) >= 6 and function_bytes[:2] == b"\xFF\x25":
                        displacement = struct.unpack_from("<i", function_bytes, 2)[0]
                        iat_rva = target_rva + 6 + displacement
                        if iat_rva in imports:
                            imported = f" import={imports[iat_rva]}"
                    print(
                        f"  slot=0x{slot * 8:04X} target_rva=0x{target_rva:08X} "
                        f"bytes={preview}{imported}"
                    )


if __name__ == "__main__":
    main()
