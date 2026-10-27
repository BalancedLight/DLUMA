#!/usr/bin/env python3
"""Tiny dependency-free PE verifier used for the prebuilt .mod."""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def cstr(data: bytes, offset: int) -> str:
    end = data.index(0, offset)
    return data[offset:end].decode("ascii", errors="replace")


def inspect(path: Path) -> dict:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise ValueError("not an MZ image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe+4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    machine, section_count, timestamp, _, _, optional_size, characteristics = struct.unpack_from("<HHIIIHH", data, pe+4)
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic != 0x20B:
        raise ValueError(f"not PE32+: 0x{magic:04X}")
    image_size = struct.unpack_from("<I", data, optional + 56)[0]
    dll_characteristics = struct.unpack_from("<H", data, optional + 70)[0]
    export_rva, export_size = struct.unpack_from("<II", data, optional + 112)
    import_rva, import_size = struct.unpack_from("<II", data, optional + 120)
    load_config_rva, load_config_size = struct.unpack_from(
        "<II", data, optional + 112 + 10 * 8
    )
    section_table = optional + optional_size
    sections = []
    for index in range(section_count):
        off = section_table + index * 40
        name = data[off:off+8].split(b"\0", 1)[0].decode("ascii", errors="replace")
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, off+8)
        sections.append((name, virtual_address, virtual_size, raw_offset, raw_size))

    def rva_to_offset(rva: int) -> int:
        for _, va, vs, raw, rs in sections:
            if va <= rva < va + max(vs, rs):
                return raw + (rva - va)
        if rva < len(data):
            return rva
        raise ValueError(f"RVA 0x{rva:X} not mapped")

    exports: list[str] = []
    if export_rva:
        ex = rva_to_offset(export_rva)
        fields = struct.unpack_from("<IIHHIIIIIII", data, ex)
        number_of_names = fields[7]
        names_rva = fields[9]
        names_off = rva_to_offset(names_rva)
        for i in range(number_of_names):
            name_rva = struct.unpack_from("<I", data, names_off + i*4)[0]
            exports.append(cstr(data, rva_to_offset(name_rva)))

    imports: list[str] = []
    imported_functions: dict[str, list[str]] = {}
    if import_rva:
        desc = rva_to_offset(import_rva)
        while True:
            original_first_thunk, stamp, chain, name_rva, first_thunk = struct.unpack_from("<IIIII", data, desc)
            if not any((original_first_thunk, stamp, chain, name_rva, first_thunk)):
                break
            dll_name = cstr(data, rva_to_offset(name_rva))
            imports.append(dll_name)
            names: list[str] = []
            thunk_rva = original_first_thunk or first_thunk
            thunk_off = rva_to_offset(thunk_rva)
            index = 0
            while True:
                value = struct.unpack_from("<Q", data, thunk_off + index * 8)[0]
                if not value:
                    break
                if value & (1 << 63):
                    names.append(f"ordinal:{value & 0xFFFF}")
                else:
                    hint_name = rva_to_offset(value)
                    names.append(cstr(data, hint_name + 2))
                index += 1
            imported_functions[dll_name] = names
            desc += 20

    security_cookie = 0
    guard_flags = 0
    if load_config_rva and load_config_size:
        load_config = rva_to_offset(load_config_rva)
        declared_size = struct.unpack_from("<I", data, load_config)[0]
        available_size = min(load_config_size, declared_size)
        if available_size >= 96:
            security_cookie = struct.unpack_from("<Q", data, load_config + 88)[0]
        if available_size >= 148:
            guard_flags = struct.unpack_from("<I", data, load_config + 144)[0]

    return {
        "path": str(path),
        "size": len(data),
        "machine": f"0x{machine:04X}",
        "timestamp": timestamp,
        "image_size": image_size,
        "is_dll": bool(characteristics & 0x2000),
        "sections": [s[0] for s in sections],
        "exports": sorted(exports),
        "imports": sorted(imports),
        "imported_functions": imported_functions,
        "export_directory_size": export_size,
        "import_directory_size": import_size,
        "dll_characteristics": f"0x{dll_characteristics:04X}",
        "dynamic_base": bool(dll_characteristics & 0x0040),
        "nx_compatible": bool(dll_characteristics & 0x0100),
        "high_entropy_va": bool(dll_characteristics & 0x0020),
        "control_flow_guard": bool(dll_characteristics & 0x4000),
        "load_config_size": load_config_size,
        "security_cookie": f"0x{security_cookie:016X}",
        "guard_flags": f"0x{guard_flags:08X}",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument(
        "--profile",
        choices=("loader", "helper", "discord-helper", "native-mod"),
        default="loader",
        help="artifact contract to validate (default: loader)",
    )
    args = parser.parse_args()
    result = inspect(args.image)
    if args.profile == "helper":
        required = {"DLUMA_Lua54_GetApi"}
        allowed_imports = {"KERNEL32.dll"}
    elif args.profile == "discord-helper":
        required = {"DLUMA_DiscordPresence_GetApi"}
        allowed_imports = {"KERNEL32.dll"}
    elif args.profile == "native-mod":
        required = {"DLUMANativeMod_GetApiV2"}
        allowed_imports = {"KERNEL32.dll"}
    else:
        required = {"InitializeGameScriptDLL", "ShutdownGameScriptDLL", "DLUMA_GetVersion"}
        allowed_imports = {"KERNEL32.dll", "USER32.dll"}
    errors = []
    if result["machine"] != "0x8664": errors.append("not x64")
    if not result["is_dll"]: errors.append("not marked DLL")
    if not required.issubset(result["exports"]): errors.append("missing required exports")
    if args.profile in {"native-mod", "discord-helper"} and \
            set(result["exports"]) != required:
        errors.append(f"unexpected {args.profile} exports")
    if set(result["imports"]) - allowed_imports: errors.append("unexpected imported DLL")
    if args.profile in {"loader", "discord-helper", "native-mod"}:
        if not result["dynamic_base"]: errors.append("ASLR/DYNAMIC_BASE is disabled")
        if not result["nx_compatible"]: errors.append("NX compatibility is disabled")
        if not result["high_entropy_va"]: errors.append("high-entropy VA is disabled")
        if not result["control_flow_guard"]: errors.append("CFG is disabled")
        if result["security_cookie"] == "0x0000000000000000":
            errors.append("load config has no security cookie")
    forbidden = {"InterlockedExchange", "InterlockedCompareExchange"}
    flat_imports = {name for names in result["imported_functions"].values() for name in names}
    if forbidden & flat_imports: errors.append("x64 compiler intrinsics were imported as procedures")
    result["valid"] = not errors
    result["profile"] = args.profile
    result["errors"] = errors
    print(json.dumps(result, indent=2) if args.json else result)
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
