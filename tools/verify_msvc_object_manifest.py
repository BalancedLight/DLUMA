#!/usr/bin/env python3
"""Reject foreign MSVC objects before the loader link step."""

from __future__ import annotations

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET


MSBUILD = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--object-dir", required=True, type=Path)
    args = parser.parse_args()

    project = args.project.resolve()
    object_dir = args.object_dir.resolve()
    expected: set[str] = set()

    def apply_manifest(manifest: Path) -> None:
        root = ET.parse(manifest).getroot()
        for tag in ("ClCompile", "MASM"):
            for item in root.findall(f".//m:{tag}", MSBUILD):
                remove = item.get("Remove")
                include = item.get("Include")
                if remove:
                    expected.discard(
                        f"{Path(remove).stem}.obj".casefold()
                    )
                if include:
                    expected.add(
                        f"{Path(include).stem}.obj".casefold()
                    )

    apply_manifest(project)
    directory_targets = project.parent / "Directory.Build.targets"
    if directory_targets.is_file():
        apply_manifest(directory_targets)

    actual = {
        path.name.casefold()
        for path in object_dir.glob("*.obj")
        if path.is_file()
    }
    unexpected = sorted(actual - expected)
    missing = sorted(expected - actual)
    if unexpected or missing:
        raise SystemExit(
            "MSVC object manifest mismatch; "
            f"missing={missing or '<none>'}, "
            f"unexpected={unexpected or '<none>'}"
        )
    print(
        f"Verified {len(actual)} explicit MSVC objects in {object_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
