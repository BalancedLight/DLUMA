#!/usr/bin/env python3
"""Statically verify the exact supported PlayerDI::OnDestroy relay window."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import sys


SUPPORTED_SHA256 = (
    "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95"
)
SUPPORTED_TIMESTAMP = 0x623397AA
SUPPORTED_IMAGE_SIZE = 0x02082000
TARGET_RVA = 0x00B06420
EXPECTED_FUNCTION_END_RVA = 0x00B06FF4
STOLEN_SIZE = 20
EXPECTED_PROLOG_SIZE = 0x1C
EXPECTED_UNWIND_CODES = bytes.fromhex(
    "1C 64 10 00 "  # save rsi at established-frame +0x80
    "1C 54 0F 00 "  # save rbp at established-frame +0x78
    "1C 34 0E 00 "  # save rbx at established-frame +0x70
    "1C 72 "        # allocate 0x40 bytes
    "18 F0 "        # push r15
    "16 E0 "        # push r14
    "14 D0 "        # push r13
    "12 C0 "        # push r12
    "10 70"         # push rdi
)
TARGET_PREFIX = bytes.fromhex(
    "48 89 5C 24 08 "
    "48 89 6C 24 10 "
    "48 89 74 24 18 "
    "57 "
    "41 54 "
    "41 55 "
    "41 56 "
    "41 57 "
    "48 83 EC 40"
)
INSTRUCTIONS = (
    (0, 5, "mov [rsp+0x08],rbx"),
    (5, 10, "mov [rsp+0x10],rbp"),
    (10, 15, "mov [rsp+0x18],rsi"),
    (15, 16, "push rdi"),
    (16, 18, "push r12"),
    (18, 20, "push r13"),
    (20, 22, "push r14"),
    (22, 24, "push r15"),
    (24, 28, "sub rsp,0x40"),
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def validate_relay_window(prefix: bytes) -> dict[str, object]:
    if len(prefix) < len(TARGET_PREFIX):
        raise ValueError("PlayerDI::OnDestroy prefix is truncated")
    if prefix[: len(TARGET_PREFIX)] != TARGET_PREFIX:
        raise ValueError("PlayerDI::OnDestroy prefix does not match")
    boundaries = [end for _, end, _ in INSTRUCTIONS]
    if STOLEN_SIZE not in boundaries:
        raise ValueError("relay window does not end on an instruction boundary")
    stolen = [
        record for record in INSTRUCTIONS if record[1] <= STOLEN_SIZE
    ]
    if not stolen or stolen[-1][1] != STOLEN_SIZE:
        raise ValueError("relay window has a partial trailing instruction")
    return {
        "stolen_size": STOLEN_SIZE,
        "instruction_boundaries": boundaries,
        "stolen_instructions": [record[2] for record in stolen],
        "relative_branch_count": 0,
        "rip_relative_operand_count": 0,
        "resume_rva": TARGET_RVA + STOLEN_SIZE,
        "resume_instruction": INSTRUCTIONS[len(stolen)][2],
        "relay_unwind_window_registered": False,
        "original_unwind_applies_after_resume": True,
    }


def parse_pe(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset > len(data) - 24 or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{path}: invalid PE signature")
    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    timestamp = struct.unpack_from("<I", data, pe_offset + 8)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    optional = pe_offset + 24
    if optional > len(data) - optional_size:
        raise ValueError(f"{path}: truncated optional header")
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise ValueError(f"{path}: expected PE32+")
    image_size = struct.unpack_from("<I", data, optional + 56)[0]
    directory_count = struct.unpack_from("<I", data, optional + 108)[0]
    if directory_count <= 3:
        raise ValueError(f"{path}: exception directory is absent")
    exception_rva, exception_size = struct.unpack_from(
        "<II", data, optional + 112 + 3 * 8
    )
    section_table = optional + optional_size
    sections = []
    for index in range(section_count):
        offset = section_table + index * 40
        if offset > len(data) - 40:
            raise ValueError(f"{path}: truncated section table")
        name = data[offset:offset + 8].split(b"\0", 1)[0].decode(
            "ascii", errors="replace"
        )
        virtual_size, virtual_address, raw_size, raw_offset = (
            struct.unpack_from("<IIII", data, offset + 8)
        )
        if raw_offset > len(data) or raw_size > len(data) - raw_offset:
            raise ValueError(f"{path}: invalid section {name}")
        sections.append(
            {
                "name": name,
                "virtual_size": virtual_size,
                "virtual_address": virtual_address,
                "raw_size": raw_size,
                "raw_offset": raw_offset,
            }
        )
    return {
        "data": data,
        "timestamp": timestamp,
        "image_size": image_size,
        "exception_rva": exception_rva,
        "exception_size": exception_size,
        "sections": sections,
    }


def rva_to_file_offset(pe: dict[str, object], rva: int, size: int) -> int:
    data = pe["data"]
    assert isinstance(data, bytes)
    sections = pe["sections"]
    assert isinstance(sections, list)
    for section in sections:
        assert isinstance(section, dict)
        start = int(section["virtual_address"])
        raw_size = int(section["raw_size"])
        virtual_size = int(section["virtual_size"])
        span = max(raw_size, virtual_size)
        if start <= rva and rva - start <= span:
            relative = rva - start
            if relative > raw_size or size > raw_size - relative:
                break
            offset = int(section["raw_offset"]) + relative
            if offset > len(data) or size > len(data) - offset:
                break
            return offset
    raise ValueError(f"RVA 0x{rva:08X} has no complete raw-file mapping")


def find_runtime_function(pe: dict[str, object]) -> tuple[int, int, int]:
    exception_rva = int(pe["exception_rva"])
    exception_size = int(pe["exception_size"])
    if exception_size == 0 or exception_size % 12:
        raise ValueError("invalid x64 exception-directory size")
    offset = rva_to_file_offset(pe, exception_rva, exception_size)
    data = pe["data"]
    assert isinstance(data, bytes)
    for entry_offset in range(offset, offset + exception_size, 12):
        begin, end, unwind = struct.unpack_from("<III", data, entry_offset)
        if begin == TARGET_RVA:
            return begin, end, unwind
    raise ValueError("PlayerDI::OnDestroy has no exact RUNTIME_FUNCTION entry")


def parse_unwind_info(pe: dict[str, object], unwind_rva: int) -> dict[str, int]:
    offset = rva_to_file_offset(
        pe, unwind_rva & ~3, 4 + len(EXPECTED_UNWIND_CODES)
    )
    data = pe["data"]
    assert isinstance(data, bytes)
    version_flags, prolog_size, code_count, frame = struct.unpack_from(
        "<BBBB", data, offset
    )
    codes = data[offset + 4:offset + 4 + code_count * 2]
    if codes != EXPECTED_UNWIND_CODES:
        raise ValueError("PlayerDI::OnDestroy unwind codes changed")
    return {
        "version": version_flags & 0x07,
        "flags": version_flags >> 3,
        "prolog_size": prolog_size,
        "unwind_code_slots": code_count,
        "frame_register": frame & 0x0F,
        "frame_offset": frame >> 4,
    }


def verify_image(path: Path) -> dict[str, object]:
    pe = parse_pe(path)
    data = pe["data"]
    assert isinstance(data, bytes)
    actual_hash = sha256_bytes(data)
    if actual_hash != SUPPORTED_SHA256:
        raise ValueError(
            f"unsupported gamedll SHA-256: {actual_hash}"
        )
    if int(pe["timestamp"]) != SUPPORTED_TIMESTAMP:
        raise ValueError("unsupported gamedll PE timestamp")
    if int(pe["image_size"]) != SUPPORTED_IMAGE_SIZE:
        raise ValueError("unsupported gamedll SizeOfImage")
    target_offset = rva_to_file_offset(pe, TARGET_RVA, len(TARGET_PREFIX))
    relay = validate_relay_window(
        data[target_offset:target_offset + len(TARGET_PREFIX)]
    )
    begin, end, unwind_rva = find_runtime_function(pe)
    if begin != TARGET_RVA or end != EXPECTED_FUNCTION_END_RVA:
        raise ValueError(
            "PlayerDI::OnDestroy RUNTIME_FUNCTION range changed"
        )
    unwind = parse_unwind_info(pe, unwind_rva)
    if unwind["version"] != 1:
        raise ValueError("PlayerDI::OnDestroy uses unknown unwind version")
    if unwind["flags"] != 0:
        raise ValueError("PlayerDI::OnDestroy has unexpected unwind flags")
    if unwind["prolog_size"] != EXPECTED_PROLOG_SIZE:
        raise ValueError("PlayerDI::OnDestroy prologue size changed")
    if STOLEN_SIZE >= unwind["prolog_size"]:
        raise ValueError("relay does not resume inside the original prologue")
    return {
        "image": str(path.resolve()),
        "sha256": actual_hash,
        "timestamp": f"0x{int(pe['timestamp']):08X}",
        "image_size": f"0x{int(pe['image_size']):08X}",
        "target_rva": f"0x{TARGET_RVA:08X}",
        "function_end_rva": f"0x{end:08X}",
        "unwind_rva": f"0x{unwind_rva:08X}",
        "unwind": unwind,
        "relay": relay,
    }


def self_test() -> dict[str, object]:
    relay = validate_relay_window(TARGET_PREFIX)
    mutated = bytearray(TARGET_PREFIX)
    mutated[0] ^= 1
    try:
        validate_relay_window(bytes(mutated))
    except ValueError:
        pass
    else:
        raise AssertionError("mutated relay prefix was accepted")
    return relay


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.image and not args.self_test:
        parser.error("one of --image or --self-test is required")
    try:
        if args.self_test:
            relay = self_test()
            print(
                "Verified PlayerDI relay fixture: "
                f"stolen={relay['stolen_size']} "
                f"resume=0x{int(relay['resume_rva']):08X} "
                "relative=0 rip_relative=0"
            )
        if args.image:
            result = verify_image(args.image.resolve())
            unwind = result["unwind"]
            assert isinstance(unwind, dict)
            print(
                "Verified exact PlayerDI::OnDestroy PE relay: "
                f"sha256={result['sha256']} "
                f"target={result['target_rva']} "
                f"range=[{result['target_rva']},"
                f"{result['function_end_rva']}) "
                f"prologue={unwind['prolog_size']} "
                f"unwind_slots={unwind['unwind_code_slots']}"
            )
        return 0
    except (OSError, ValueError, AssertionError) as error:
        print(f"PlayerDI relay verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
