#!/usr/bin/env python3
"""Fail closed unless a loader package contains the exact deployable files."""

from __future__ import annotations

import argparse
from pathlib import Path

from build_managed_runtime import (
    EDITOR_THEME_ARTIFACTS,
    RUNTIME_VERSION,
    read_catalog_hash,
    verify_runtime,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", required=True, type=Path)
    parser.add_argument("--mod-name", required=True)
    parser.add_argument("--require-editor-wiki", action="store_true")
    parser.add_argument("--require-editor-git", action="store_true")
    parser.add_argument(
        "--catalog-hash",
        type=Path,
        default=Path(__file__).resolve().parents[1]
        / "api"
        / "generated"
        / "dluma_api_catalog.sha256",
    )
    args = parser.parse_args()

    package_dir = args.package_dir.resolve()
    if any(path.is_file() and path.name.casefold() == "dluma.ini"
           for path in package_dir.rglob("*")):
        raise SystemExit("release packages must not contain user configuration: dluma.ini")
    expected = {
        args.mod_name,
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "dluma.pak",
        "dluma_lua54.dll",
        "LICENSE_LUA_5.4.8.txt",
        "dluma_discord_presence.dll",
        "dluma_nightly_updater.exe",
    }
    if not package_dir.is_dir():
        raise SystemExit(f"package directory does not exist: {package_dir}")
    actual = {
        path.name
        for path in package_dir.iterdir()
        if path.is_file()
    }
    actual_directories = {
        path.name
        for path in package_dir.iterdir()
        if path.is_dir()
    }
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    expected_directories = {"dluma_runtime"}
    missing_directories = sorted(expected_directories - actual_directories)
    unexpected_directories = sorted(actual_directories - expected_directories)
    if missing or unexpected or missing_directories or unexpected_directories:
        raise SystemExit(
            "package manifest mismatch; "
            f"missing={missing or '<none>'}, "
            f"unexpected={unexpected or '<none>'}, "
            f"missing_directories={missing_directories or '<none>'}, "
            f"unexpected_directories={unexpected_directories or '<none>'}"
        )
    for name in expected:
        if (package_dir / name).stat().st_size <= 0:
            raise SystemExit(f"package file is empty: {name}")
    try:
        runtime_manifest = verify_runtime(
            package_dir,
            expected_catalog_hash=read_catalog_hash(args.catalog_hash),
            expected_runtime_version=RUNTIME_VERSION,
            require_editor_wiki=args.require_editor_wiki,
            require_editor_git=args.require_editor_git,
        )
        runtime_paths = {
            str(record.get("path", "")).casefold()
            for record in runtime_manifest.get("files", [])
            if isinstance(record, dict)
        }
        required_editor = {
            f"editor/{name}".casefold()
            for name in EDITOR_THEME_ARTIFACTS
            if name.casefold().endswith(".dll")
        }
        editor_dlls = {
            path for path in runtime_paths
            if path.startswith("editor/") and path.endswith(".dll")
        }
        if editor_dlls != required_editor:
            raise ValueError("runtime manifest has missing or unexpected Editor appearance libraries")
        required_diagnostics = {
            "diagnostics/loader_address_ranges.json",
            "updater/apply_nightly_update.ps1",
        }
        missing_diagnostics = sorted(
            path
            for path in required_diagnostics
            if path.casefold() not in runtime_paths
        )
        if missing_diagnostics:
            raise ValueError(
                "runtime manifest omits loader diagnostics: "
                f"{missing_diagnostics}"
            )
    except (OSError, ValueError) as error:
        raise SystemExit(f"managed runtime package verification failed: {error}")
    print(f"Verified exact package manifest: {package_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
