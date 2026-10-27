#!/usr/bin/env python3
"""Generate and embed a compact no-DbgHelp loader RVA symbol table."""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Iterable


MAGIC = b"DLWSYM1\0"
VERSION = 1
CAPACITY = 1024 * 1024
HEADER_FORMAT = "<8s10I"
ENTRY_FORMAT = "<IIIIIHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
ENTRY_SIZE = struct.calcsize(ENTRY_FORMAT)
HASH_FIELD_OFFSET = struct.calcsize("<8s9I")
SECTION_NAME = b".dlwsym"
CLASS_FUNCTION_BODY = 1
CLASS_EPILOGUE = 2
CLASS_GENERATED_THUNK = 3
CLASS_RELAY = 4

SYMOPT_UNDNAME = 0x00000002
SYMOPT_DEFERRED_LOADS = 0x00000004
SYMOPT_LOAD_LINES = 0x00000010
SYMOPT_FAIL_CRITICAL_ERRORS = 0x00000200
SYMOPT_NO_PROMPTS = 0x00080000
SYMFLAG_FUNCTION = 0x00000800
SYMTAG_FUNCTION = 5
MAX_SYM_NAME = 4096


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def fnv1a32_with_zeroed_hash(blob: bytes) -> int:
    value = 2166136261
    for index, byte in enumerate(blob):
        if HASH_FIELD_OFFSET <= index < HASH_FIELD_OFFSET + 4:
            byte = 0
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def parse_pe(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset > len(data) - 24 or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{path}: invalid PE header")
    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    timestamp = struct.unpack_from("<I", data, pe_offset + 8)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    optional = pe_offset + 24
    if optional > len(data) - optional_size or optional_size < 64:
        raise ValueError(f"{path}: truncated optional header")
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic != 0x20B:
        raise ValueError(f"{path}: expected PE32+")
    image_base = struct.unpack_from("<Q", data, optional + 24)[0]
    image_size = struct.unpack_from("<I", data, optional + 56)[0]
    section_table = optional + optional_size
    sections = []
    for index in range(section_count):
        offset = section_table + index * 40
        if offset > len(data) - 40:
            raise ValueError(f"{path}: truncated section table")
        name = data[offset:offset + 8].split(b"\0", 1)[0]
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, offset + 8
        )
        if raw_offset > len(data) or raw_size > len(data) - raw_offset:
            raise ValueError(f"{path}: invalid raw section range {name!r}")
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
        "image_base": image_base,
        "image_size": image_size,
        "sections": sections,
    }


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.ULONG),
        ("TypeIndex", wintypes.ULONG),
        ("Reserved", ctypes.c_ulonglong * 2),
        ("Index", wintypes.ULONG),
        ("Size", wintypes.ULONG),
        ("ModBase", ctypes.c_ulonglong),
        ("Flags", wintypes.ULONG),
        ("Value", ctypes.c_ulonglong),
        ("Address", ctypes.c_ulonglong),
        ("Register", wintypes.ULONG),
        ("Scope", wintypes.ULONG),
        ("Tag", wintypes.ULONG),
        ("NameLen", wintypes.ULONG),
        ("MaxNameLen", wintypes.ULONG),
        ("Name", ctypes.c_char * 1),
    ]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [
        ("SizeOfStruct", wintypes.DWORD),
        ("Key", ctypes.c_void_p),
        ("LineNumber", wintypes.DWORD),
        ("FileName", ctypes.c_char_p),
        ("Address", ctypes.c_ulonglong),
    ]


def clean_text(value: str, fallback: str) -> str:
    cleaned = value.replace("\0", "").replace("\r", " ").replace("\n", " ")
    return cleaned or fallback


def relative_source_path(path: str, source_root: Path | None) -> str:
    cleaned = clean_text(path, "<source-unavailable>")
    if cleaned.startswith("<"):
        return cleaned
    if not source_root:
        return Path(cleaned.replace("\\", "/")).name
    try:
        return str(Path(cleaned).resolve().relative_to(source_root.resolve())).replace(
            "\\", "/"
        )
    except (OSError, ValueError):
        return Path(cleaned.replace("\\", "/")).name


class DbgHelpSymbols:
    def __init__(
        self, image: Path, pdb: Path, source_root: Path | None
    ):
        if sys.platform != "win32":
            raise RuntimeError("DbgHelp symbol generation requires Windows")
        self.image = image
        self.pdb = pdb
        self.source_root = source_root
        self.dbghelp = ctypes.WinDLL("dbghelp.dll", use_last_error=True)
        self.kernel32 = ctypes.WinDLL("kernel32.dll", use_last_error=True)
        self.process = None
        self.base = 0
        self._configure_api()
        self.process = self.kernel32.GetCurrentProcess()

    def _configure_api(self) -> None:
        self.kernel32.GetCurrentProcess.argtypes = []
        self.kernel32.GetCurrentProcess.restype = wintypes.HANDLE
        self.dbghelp.SymSetOptions.argtypes = [wintypes.DWORD]
        self.dbghelp.SymSetOptions.restype = wintypes.DWORD
        self.dbghelp.SymInitializeW.argtypes = [
            wintypes.HANDLE, wintypes.LPCWSTR, wintypes.BOOL
        ]
        self.dbghelp.SymInitializeW.restype = wintypes.BOOL
        self.dbghelp.SymCleanup.argtypes = [wintypes.HANDLE]
        self.dbghelp.SymCleanup.restype = wintypes.BOOL
        self.dbghelp.SymLoadModuleExW.argtypes = [
            wintypes.HANDLE,
            wintypes.HANDLE,
            wintypes.LPCWSTR,
            wintypes.LPCWSTR,
            ctypes.c_ulonglong,
            wintypes.DWORD,
            ctypes.c_void_p,
            wintypes.DWORD,
        ]
        self.dbghelp.SymLoadModuleExW.restype = ctypes.c_ulonglong
        self.dbghelp.SymFromAddr.argtypes = [
            wintypes.HANDLE,
            ctypes.c_ulonglong,
            ctypes.POINTER(ctypes.c_ulonglong),
            ctypes.POINTER(SYMBOL_INFO),
        ]
        self.dbghelp.SymFromAddr.restype = wintypes.BOOL
        self.dbghelp.SymGetLineFromAddr64.argtypes = [
            wintypes.HANDLE,
            ctypes.c_ulonglong,
            ctypes.POINTER(wintypes.DWORD),
            ctypes.POINTER(IMAGEHLP_LINE64),
        ]
        self.dbghelp.SymGetLineFromAddr64.restype = wintypes.BOOL

    def __enter__(self) -> "DbgHelpSymbols":
        options = (
            SYMOPT_UNDNAME
            | SYMOPT_DEFERRED_LOADS
            | SYMOPT_LOAD_LINES
            | SYMOPT_FAIL_CRITICAL_ERRORS
            | SYMOPT_NO_PROMPTS
        )
        self.dbghelp.SymSetOptions(options)
        if not self.dbghelp.SymInitializeW(
            self.process, str(self.pdb.parent), False
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        self.base = self.dbghelp.SymLoadModuleExW(
            self.process, None, str(self.image), None, 0, 0, None, 0
        )
        if not self.base:
            error = ctypes.get_last_error()
            self.dbghelp.SymCleanup(self.process)
            raise ctypes.WinError(error)
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.dbghelp.SymCleanup(self.process)

    def _symbol_buffer(self) -> tuple[ctypes.Array, ctypes.POINTER(SYMBOL_INFO)]:
        buffer = ctypes.create_string_buffer(
            ctypes.sizeof(SYMBOL_INFO) + MAX_SYM_NAME
        )
        symbol = ctypes.cast(buffer, ctypes.POINTER(SYMBOL_INFO))
        symbol.contents.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO)
        symbol.contents.MaxNameLen = MAX_SYM_NAME
        return buffer, symbol

    @staticmethod
    def _name(symbol: SYMBOL_INFO) -> str:
        address = ctypes.addressof(symbol) + SYMBOL_INFO.Name.offset
        raw = ctypes.string_at(address, symbol.NameLen)
        return clean_text(raw.decode("utf-8", errors="replace"), "<unnamed>")

    def source_line(self, address: int) -> tuple[str, int]:
        displacement = wintypes.DWORD()
        line = IMAGEHLP_LINE64()
        line.SizeOfStruct = ctypes.sizeof(line)
        if not self.dbghelp.SymGetLineFromAddr64(
            self.process, address, ctypes.byref(displacement), ctypes.byref(line)
        ):
            return "<source-unavailable>", 0
        filename = (
            line.FileName.decode("utf-8", errors="replace")
            if line.FileName
            else "<source-unavailable>"
        )
        return (
            relative_source_path(filename, self.source_root),
            int(line.LineNumber),
        )

    def symbolicate(self, rva: int) -> dict:
        buffer, symbol = self._symbol_buffer()
        displacement = ctypes.c_ulonglong()
        address = self.base + rva
        if not self.dbghelp.SymFromAddr(
            self.process, address, ctypes.byref(displacement), symbol
        ):
            raise RuntimeError(
                f"SymFromAddr failed for RVA 0x{rva:08X}: "
                f"WinError {ctypes.get_last_error()}"
            )
        info = symbol.contents
        start = int(info.Address - self.base)
        size = int(info.Size)
        end = start + size if size else start + 1
        source_file, source_line = self.source_line(address)
        name = self._name(info)
        return {
            "query_rva": rva,
            "start_rva": start,
            "end_rva": end,
            "size": size,
            "displacement": int(displacement.value),
            "name": name,
            "source_file": source_file,
            "source_line": source_line,
            "classification": classify_symbol(name, rva, start, end, self.image),
        }

    def enumerate_functions(self) -> list[dict]:
        callback_type = ctypes.WINFUNCTYPE(
            wintypes.BOOL,
            ctypes.POINTER(SYMBOL_INFO),
            wintypes.ULONG,
            ctypes.c_void_p,
        )
        records: list[dict] = []

        def callback(symbol_pointer, symbol_size, context):
            del symbol_size, context
            info = symbol_pointer.contents
            if (
                info.Address < self.base
                or not (info.Flags & SYMFLAG_FUNCTION or info.Tag == SYMTAG_FUNCTION)
            ):
                return True
            start = int(info.Address - self.base)
            if start > 0xFFFFFFFF:
                return True
            source_file, source_line = self.source_line(info.Address)
            records.append(
                {
                    "start_rva": start,
                    "end_rva": start + int(info.Size) if info.Size else start + 1,
                    "size": int(info.Size),
                    "name": self._name(info),
                    "source_file": source_file,
                    "source_line": source_line,
                }
            )
            return True

        callback_function = callback_type(callback)
        self.dbghelp.SymEnumSymbols.argtypes = [
            wintypes.HANDLE,
            ctypes.c_ulonglong,
            ctypes.c_char_p,
            callback_type,
            ctypes.c_void_p,
        ]
        self.dbghelp.SymEnumSymbols.restype = wintypes.BOOL
        if not self.dbghelp.SymEnumSymbols(
            self.process, self.base, None, callback_function, None
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        records.sort(key=lambda item: (item["start_rva"], item["name"]))
        for index, record in enumerate(records):
            if record["size"]:
                continue
            next_start = (
                records[index + 1]["start_rva"]
                if index + 1 < len(records)
                else record["start_rva"] + 1
            )
            record["end_rva"] = max(record["start_rva"] + 1, next_start)
        return records


def classify_symbol(
    name: str, rva: int, start: int, end: int, image: Path
) -> str:
    del image
    lowered = name.lower()
    if "thunk" in lowered or lowered.startswith("@ilt+"):
        return "generated_thunk"
    if "relay" in lowered and "prepare" not in lowered:
        return "relay"
    if end > start and rva >= max(start, end - 8):
        return "epilogue"
    return "function_body"


def classification_value(name: str) -> int:
    return {
        "function_body": CLASS_FUNCTION_BODY,
        "epilogue": CLASS_EPILOGUE,
        "generated_thunk": CLASS_GENERATED_THUNK,
        "relay": CLASS_RELAY,
    }.get(name, CLASS_FUNCTION_BODY)


def add_string(pool: bytearray, offsets: dict[str, int], value: str) -> int:
    value = clean_text(value, "<unavailable>")
    existing = offsets.get(value)
    if existing is not None:
        return existing
    offset = len(pool)
    encoded = value.encode("utf-8", errors="replace")
    if len(encoded) > 4095:
        encoded = encoded[:4095]
    pool.extend(encoded)
    pool.append(0)
    offsets[value] = offset
    return offset


def encode_manifest(
    records: Iterable[dict],
    timestamp: int,
    image_size: int,
    capacity: int = CAPACITY,
) -> bytes:
    normalized = list(records)
    entries_offset = HEADER_SIZE
    strings_offset = entries_offset + len(normalized) * ENTRY_SIZE
    pool = bytearray()
    string_offsets: dict[str, int] = {}
    entry_values = []
    for record in normalized:
        name_local = add_string(pool, string_offsets, record["name"])
        file_local = add_string(pool, string_offsets, record["source_file"])
        entry_values.append(
            (
                int(record["start_rva"]),
                int(record["end_rva"]),
                int(record.get("source_line", 0)),
                strings_offset + name_local,
                strings_offset + file_local,
                classification_value(record.get("classification", "function_body")),
                0,
            )
        )
    total_size = strings_offset + len(pool)
    if total_size > capacity:
        raise ValueError(
            f"loader symbol manifest needs {total_size} bytes; "
            f"capacity is {capacity}"
        )
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        HEADER_SIZE,
        total_size,
        capacity,
        len(entry_values),
        entries_offset,
        strings_offset,
        timestamp,
        image_size,
        0,
    )
    blob = bytearray(header)
    for entry in entry_values:
        blob.extend(struct.pack(ENTRY_FORMAT, *entry))
    blob.extend(pool)
    table_hash = fnv1a32_with_zeroed_hash(blob)
    struct.pack_into("<I", blob, HASH_FIELD_OFFSET, table_hash)
    return bytes(blob)


def lookup_encoded_manifest(blob: bytes, rva: int) -> dict | None:
    if len(blob) < HEADER_SIZE:
        return None
    fields = struct.unpack_from(HEADER_FORMAT, blob)
    (
        magic,
        version,
        header_size,
        total_size,
        capacity,
        entry_count,
        entries_offset,
        strings_offset,
        timestamp,
        image_size,
        table_hash,
    ) = fields
    del capacity, timestamp, image_size
    if (
        magic != MAGIC
        or version != VERSION
        or header_size != HEADER_SIZE
        or total_size > len(blob)
        or strings_offset != entries_offset + entry_count * ENTRY_SIZE
        or table_hash != fnv1a32_with_zeroed_hash(blob[:total_size])
    ):
        return None
    best = None
    for index in range(entry_count):
        entry = struct.unpack_from(
            ENTRY_FORMAT, blob, entries_offset + index * ENTRY_SIZE
        )
        start, end = entry[0], entry[1]
        if start <= rva < end and (
            best is None or end - start < best[1] - best[0]
        ):
            best = entry
    if best is None:
        return None

    def read_string(offset: int) -> str:
        if offset < strings_offset or offset >= total_size:
            raise ValueError("invalid string offset")
        end = blob.find(b"\0", offset, total_size)
        if end < 0:
            raise ValueError("unterminated string")
        return blob[offset:end].decode("utf-8", errors="replace")

    return {
        "query_rva": rva,
        "start_rva": best[0],
        "end_rva": best[1],
        "source_line": best[2],
        "name": read_string(best[3]),
        "source_file": read_string(best[4]),
        "classification": best[5],
    }


def patch_image(image: Path, pe: dict, blob: bytes) -> None:
    section = next(
        (item for item in pe["sections"] if item["name"] == SECTION_NAME),
        None,
    )
    if not section:
        raise ValueError(f"{image}: reserved {SECTION_NAME!r} section missing")
    if section["raw_size"] < CAPACITY:
        raise ValueError(
            f"{image}: {SECTION_NAME!r} raw size "
            f"{section['raw_size']} is smaller than {CAPACITY}"
        )
    data = bytearray(pe["data"])
    start = section["raw_offset"]
    data[start:start + CAPACITY] = blob + b"\0" * (CAPACITY - len(blob))
    image.write_bytes(data)


def build_runtime_records(
    functions: list[dict], required: list[dict]
) -> list[dict]:
    records = []
    seen = set()
    for item in functions:
        if item["end_rva"] <= item["start_rva"]:
            continue
        record = dict(item)
        record["classification"] = classify_symbol(
            record["name"],
            record["start_rva"],
            record["start_rva"],
            record["end_rva"],
            Path(),
        )
        key = (
            record["start_rva"],
            record["end_rva"],
            record["name"],
            record["source_file"],
        )
        if key not in seen:
            seen.add(key)
            records.append(record)
    for item in required:
        exact = dict(item)
        exact["start_rva"] = exact["query_rva"]
        exact["end_rva"] = exact["query_rva"] + 1
        key = (
            exact["start_rva"],
            exact["end_rva"],
            exact["name"],
            exact["source_file"],
        )
        if key not in seen:
            seen.add(key)
            records.append(exact)
    records.sort(
        key=lambda item: (
            item["start_rva"],
            item["end_rva"],
            item["name"],
        )
    )
    return records


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", required=True, type=Path)
    parser.add_argument("--pdb", required=True, type=Path)
    parser.add_argument("--map", dest="map_path", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-root", type=Path)
    parser.add_argument(
        "--required-rva",
        action="append",
        default=[],
        help="required loader RVA, decimal or 0x-prefixed",
    )
    parser.add_argument("--no-patch", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    image = args.image.resolve()
    pdb = args.pdb.resolve()
    map_path = args.map_path.resolve() if args.map_path else None
    output = args.output.resolve()
    for path, label in ((image, "image"), (pdb, "PDB")):
        if not path.is_file():
            raise FileNotFoundError(f"{label} not found: {path}")
    if map_path and not map_path.is_file():
        raise FileNotFoundError(f"MAP not found: {map_path}")
    required_rvas = [int(value, 0) for value in args.required_rva]
    required_rvas = list(dict.fromkeys(required_rvas))
    pe = parse_pe(image)
    with DbgHelpSymbols(image, pdb, args.source_root) as symbols:
        required = [symbols.symbolicate(rva) for rva in required_rvas]
        functions = symbols.enumerate_functions()
    for item in required:
        if not item["name"] or not item["source_line"]:
            raise RuntimeError(
                f"RVA 0x{item['query_rva']:08X} lacks an exact "
                "symbol or source line"
            )
    records = build_runtime_records(functions, required)
    blob = encode_manifest(
        records, pe["timestamp"], pe["image_size"], CAPACITY
    )
    for rva in required_rvas:
        if lookup_encoded_manifest(blob, rva) is None:
            raise RuntimeError(
                f"encoded manifest does not resolve RVA 0x{rva:08X}"
            )
    image_hash_before = sha256_file(image)
    if not args.no_patch:
        patch_image(image, pe, blob)
    image_hash_after = sha256_file(image)
    report = {
        "schema": "DLUMA_LOADER_ADDRESS_RANGE_MANIFEST_V1",
        "image_name": image.name,
        "image_sha256_before_patch": image_hash_before,
        "image_sha256": image_hash_after,
        "image_timestamp": f"0x{pe['timestamp']:08X}",
        "image_size": f"0x{pe['image_size']:08X}",
        "pdb_name": pdb.name,
        "pdb_sha256": sha256_file(pdb),
        "map_name": map_path.name if map_path else None,
        "map_sha256": sha256_file(map_path) if map_path else None,
        "embedded": not args.no_patch,
        "entry_count": len(records),
        "manifest_bytes": len(blob),
        "required_frames": required,
        "ranges": records,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    for item in required:
        print(
            f"loader+0x{item['query_rva']:X} "
            f"{item['name']} "
            f"{item['source_file']}:{item['source_line']} "
            f"[0x{item['start_rva']:X},0x{item['end_rva']:X}) "
            f"{item['classification']}"
        )
    print(
        f"Generated {len(records)} loader symbol ranges; "
        f"embedded_bytes={len(blob)} image_sha256={image_hash_after}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
