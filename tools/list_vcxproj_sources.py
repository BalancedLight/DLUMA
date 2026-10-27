#!/usr/bin/env python3
"""Emit the checked-in MSBuild C/C++ source manifest in build order."""

from __future__ import annotations

import argparse
from collections import OrderedDict
from pathlib import Path, PureWindowsPath
import xml.etree.ElementTree as ET


MSBUILD = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def normalized(path: str) -> str:
    return str(PureWindowsPath(path)).casefold()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--kind", required=True, choices=("c", "cpp"))
    parser.add_argument("--exclude", action="append", default=[])
    args = parser.parse_args()

    project = args.project.resolve()
    sources: OrderedDict[str, tuple[str, str]] = OrderedDict()

    def apply_manifest(manifest: Path) -> None:
        root = ET.parse(manifest).getroot()
        for item in root.findall(".//m:ClCompile", MSBUILD):
            remove = item.get("Remove")
            include = item.get("Include")
            if remove:
                sources.pop(normalized(remove), None)
            if not include:
                continue
            compile_as = item.find("m:CompileAs", MSBUILD)
            kind = (
                "cpp"
                if Path(include).suffix.casefold() == ".cpp"
                or (
                    compile_as is not None
                    and compile_as.text == "CompileAsCpp"
                )
                else "c"
            )
            sources[normalized(include)] = (include, kind)

    apply_manifest(project)
    directory_targets = project.parent / "Directory.Build.targets"
    if directory_targets.is_file():
        apply_manifest(directory_targets)

    exclusions = {normalized(value) for value in args.exclude}
    for key, (source, kind) in sources.items():
        if kind == args.kind and key not in exclusions:
            print(source)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
