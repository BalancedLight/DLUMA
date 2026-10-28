#!/usr/bin/env python3
"""Stage and package the Third Person native mod."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
from pathlib import Path

from dluma_mod_package import PackageError, create_package, validate_lock, write_lock

ROOT = Path(__file__).resolve().parents[1]
MOD_RELATIVE = Path("mods/native/global/thirdperson.camera")


def inspect_module(path: Path) -> dict[str, object]:
    image = path.read_bytes()
    if image[:2] != b"MZ" or len(image) < 64:
        raise PackageError("The native module is not a PE image")
    pe = struct.unpack_from("<I", image, 60)[0]
    if pe + 264 > len(image) or image[pe:pe + 4] != b"PE\0\0":
        raise PackageError("The native module has an invalid PE header")
    machine, count = struct.unpack_from("<HH", image, pe + 4)
    optional_size = struct.unpack_from("<H", image, pe + 20)[0]
    optional = pe + 24
    if machine != 0x8664 or struct.unpack_from("<H", image, optional)[0] != 0x20B:
        raise PackageError("Third Person requires an x64 PE32+ module")
    sections = []
    for index in range(count):
        section = optional + optional_size + index * 40
        if section + 40 > len(image):
            raise PackageError("The native module has an incomplete section table")
        virtual_size, rva, size, offset = struct.unpack_from("<IIII", image, section + 8)
        sections.append((rva, size, offset))

    def file_offset(rva: int, size: int) -> int:
        for start, length, offset in sections:
            if start <= rva and rva + size <= start + length:
                result = offset + rva - start
                if result + size <= len(image):
                    return result
        raise PackageError("The native module has an invalid export address")

    export_rva, export_size = struct.unpack_from("<II", image, optional + 112)
    if export_size < 40:
        raise PackageError("The native module has no export table")
    export = file_offset(export_rva, 40)
    names_count = struct.unpack_from("<I", image, export + 24)[0]
    names = struct.unpack_from("<I", image, export + 32)[0]
    names_offset = file_offset(names, names_count * 4)
    exports = []
    for index in range(names_count):
        name_rva = struct.unpack_from("<I", image, names_offset + 4 * index)[0]
        name_offset = file_offset(name_rva, 1)
        end = image.find(b"\0", name_offset, name_offset + 512)
        if end < 0:
            raise PackageError("The native module has an invalid export name")
        exports.append(image[name_offset:end].decode("ascii"))
    if exports != ["DLUMANativeMod_GetApiV2"]:
        raise PackageError("The native module must export only DLUMANativeMod_GetApiV2")
    return {"architecture": "x64", "exports": exports}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", type=Path, required=True)
    args = parser.parse_args()
    try:
        module = args.module.resolve(strict=True)
        report = inspect_module(module)
        source = ROOT / "package" / MOD_RELATIVE
        destination = ROOT / "dist" / MOD_RELATIVE
        destination.mkdir(parents=True, exist_ok=True)
        for filename in ("mod.dluma.json", "config.dluma.json", "README.txt", "LICENSE-MINHOOK.txt"):
            shutil.copyfile(source / filename, destination / filename)
        shutil.copyfile(module, destination / "ThirdPerson.dll")
        write_lock(destination)
        lock = validate_lock(destination)
        version = json.loads((source / "mod.dluma.json").read_text(encoding="utf-8"))["version"]
        package = create_package(destination, ROOT / f"dist/ThirdPerson-{version}.zip")
        print(json.dumps({"module": report, "artifacts": len(lock["artifacts"]), "package": package}, indent=2))
        return 0
    except (OSError, ValueError, struct.error, PackageError) as error:
        parser.exit(1, f"Third Person package error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
