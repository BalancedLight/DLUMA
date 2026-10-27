#!/usr/bin/env python3
"""Assemble and verify the pinned private DLUMA managed runtime tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable


RUNTIME_SCHEMA = "dluma.runtime-tree.v1"
RUNTIME_VERSION = "10.0.13"
API_VERSION = "1.0"
RUNTIME_RELATIVE_ROOT = Path("dluma_runtime")
MANIFEST_NAME = "runtime-manifest.json"
MAXIMUM_RUNTIME_FILES = 512
QUEST_GRAPH_ARTIFACTS = (
    "Dluma.QuestGraph.exe",
    "Dluma.QuestGraph.dll",
    "Dluma.QuestGraph.deps.json",
    "Dluma.QuestGraph.runtimeconfig.json",
    "Dluma.QuestGraph.Core.dll",
    "Nodify.dll",
    "THIRD_PARTY_NOTICES.md",
    "Licenses/Nodify-MIT.txt",
)


@dataclass(frozen=True)
class SourceFile:
    source: Path
    destination: PurePosixPath


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def resolve_dotnet_root(value: Path | None) -> Path:
    candidates: list[Path] = []
    if value is not None:
        candidates.append(value)
    for name in ("DOTNET_ROOT_X64", "DOTNET_ROOT"):
        raw = os.environ.get(name)
        if raw:
            candidates.append(Path(raw))
    program_files = os.environ.get("ProgramFiles")
    if program_files:
        candidates.append(Path(program_files) / "dotnet")
    candidates.append(Path(r"C:\Program Files\dotnet"))

    for candidate in candidates:
        resolved = candidate.expanduser().resolve()
        if resolved.is_dir():
            return resolved
    rendered = ", ".join(str(candidate) for candidate in candidates)
    raise ValueError(f"could not locate a .NET installation; checked: {rendered}")


def require_regular_file(path: Path, description: str) -> Path:
    resolved = path.resolve()
    if not resolved.is_file():
        raise ValueError(f"{description} is missing: {resolved}")
    if resolved.stat().st_size <= 0:
        raise ValueError(f"{description} is empty: {resolved}")
    return resolved


def collect_sources(
    dotnet_root: Path,
    managed_output: Path,
    runtime_version: str,
    quest_graph_output: Path,
    loader_symbol_manifest: Path | None = None,
) -> list[SourceFile]:
    hostfxr = require_regular_file(
        dotnet_root / "host" / "fxr" / runtime_version / "hostfxr.dll",
        "pinned hostfxr",
    )
    framework_root = (
        dotnet_root / "shared" / "Microsoft.NETCore.App" / runtime_version
    ).resolve()
    if not framework_root.is_dir():
        raise ValueError(f"pinned framework is missing: {framework_root}")
    desktop_framework_root = (
        dotnet_root / "shared" / "Microsoft.WindowsDesktop.App" / runtime_version
    ).resolve()
    if not desktop_framework_root.is_dir():
        raise ValueError(
            f"pinned Windows Desktop framework is missing: {desktop_framework_root}"
        )

    files: list[SourceFile] = [
        SourceFile(
            require_regular_file(dotnet_root / "dotnet.exe", "pinned dotnet host"),
            PurePosixPath("dotnet/dotnet.exe"),
        ),
        SourceFile(
            hostfxr,
            PurePosixPath("dotnet/host/fxr")
            / runtime_version
            / "hostfxr.dll",
        ),
        SourceFile(
            require_regular_file(dotnet_root / "LICENSE.txt", ".NET license"),
            PurePosixPath("licenses/dotnet/LICENSE.txt"),
        ),
        SourceFile(
            require_regular_file(
                dotnet_root / "ThirdPartyNotices.txt",
                ".NET third-party notices",
            ),
            PurePosixPath("licenses/dotnet/ThirdPartyNotices.txt"),
        ),
    ]

    framework_files = sorted(
        (path for path in framework_root.rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(framework_root).as_posix().casefold(),
    )
    if not framework_files:
        raise ValueError(f"pinned framework contains no files: {framework_root}")
    for source in framework_files:
        relative = PurePosixPath(source.relative_to(framework_root).as_posix())
        files.append(
            SourceFile(
                source.resolve(),
                PurePosixPath("dotnet/shared/Microsoft.NETCore.App")
                / runtime_version
                / relative,
            )
        )

    desktop_framework_files = sorted(
        (
            path
            for path in desktop_framework_root.rglob("*")
            if path.is_file()
        ),
        key=lambda path: path.relative_to(
            desktop_framework_root
        ).as_posix().casefold(),
    )
    if not desktop_framework_files:
        raise ValueError(
            f"pinned Windows Desktop framework contains no files: "
            f"{desktop_framework_root}"
        )
    for source in desktop_framework_files:
        relative = PurePosixPath(
            source.relative_to(desktop_framework_root).as_posix()
        )
        files.append(
            SourceFile(
                source.resolve(),
                PurePosixPath(
                    "dotnet/shared/Microsoft.WindowsDesktop.App"
                )
                / runtime_version
                / relative,
            )
        )

    managed_output = managed_output.resolve()
    managed_names = (
        "Dluma.Managed.Bootstrap.dll",
        "Dluma.Managed.Bootstrap.deps.json",
        "Dluma.Managed.Bootstrap.runtimeconfig.json",
        "Dluma.Managed.Sdk.dll",
        "Dluma.Managed.Sdk.xml",
    )
    for name in managed_names:
        files.append(
            SourceFile(
                require_regular_file(
                    managed_output / name, f"managed runtime artifact {name}"
                ),
                PurePosixPath("managed") / name,
            )
        )

    quest_graph_output = quest_graph_output.resolve()
    for name in QUEST_GRAPH_ARTIFACTS:
        files.append(
            SourceFile(
                require_regular_file(
                    quest_graph_output.joinpath(*PurePosixPath(name).parts),
                    f"visual quest editor artifact {name}",
                ),
                PurePosixPath("quest-graph") / PurePosixPath(name),
            )
        )
    if loader_symbol_manifest is not None:
        files.append(
            SourceFile(
                require_regular_file(
                    loader_symbol_manifest,
                    "loader address-range manifest",
                ),
                PurePosixPath("diagnostics/loader_address_ranges.json"),
            )
        )
    destinations: set[str] = set()
    for item in files:
        normalized = item.destination.as_posix()
        folded = normalized.casefold()
        if item.destination.is_absolute() or ".." in item.destination.parts:
            raise ValueError(f"unsafe runtime destination: {normalized}")
        if folded in destinations:
            raise ValueError(f"duplicate runtime destination: {normalized}")
        destinations.add(folded)
    if len(files) > MAXIMUM_RUNTIME_FILES:
        raise ValueError(
            "private runtime exceeds the native manifest admission limit: "
            f"{len(files)} > {MAXIMUM_RUNTIME_FILES}"
        )
    return sorted(files, key=lambda item: item.destination.as_posix().casefold())


def read_catalog_hash(path: Path) -> str:
    value = require_regular_file(path, "DLUMA API catalog hash").read_text(
        encoding="ascii"
    ).strip()
    if len(value) != 64 or any(character not in "0123456789abcdefABCDEF" for character in value):
        raise ValueError(f"invalid DLUMA API catalog hash: {path.resolve()}")
    return value.upper()


def build_manifest(
    sources: Iterable[SourceFile],
    runtime_version: str,
    catalog_hash: str,
) -> dict[str, object]:
    records = []
    for item in sources:
        records.append(
            {
                "path": item.destination.as_posix(),
                "size": item.source.stat().st_size,
                "sha256": sha256_file(item.source),
            }
        )
    return {
        "schema": RUNTIME_SCHEMA,
        "api_version": API_VERSION,
        "catalog_sha256": catalog_hash,
        "runtime": {
            "deployment": "private-framework-dependent",
            "framework": "Microsoft.NETCore.App",
            "rid": "win-x64",
            "version": runtime_version,
        },
        "files": records,
    }


def encoded_manifest(manifest: dict[str, object]) -> bytes:
    return (
        json.dumps(
            manifest,
            ensure_ascii=False,
            indent=2,
            sort_keys=False,
        )
        + "\n"
    ).encode("utf-8")


def ensure_runtime_child(output_dir: Path) -> Path:
    output_dir = output_dir.resolve()
    runtime_root = (output_dir / RUNTIME_RELATIVE_ROOT).resolve()
    if runtime_root.parent != output_dir:
        raise ValueError(f"unsafe managed runtime output: {runtime_root}")
    return runtime_root


def write_runtime(
    output_dir: Path,
    sources: list[SourceFile],
    manifest: dict[str, object],
) -> None:
    runtime_root = ensure_runtime_child(output_dir)
    if runtime_root.exists():
        shutil.rmtree(runtime_root)
    runtime_root.mkdir(parents=True)
    for item in sources:
        destination = runtime_root.joinpath(*item.destination.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item.source, destination)
    (runtime_root / MANIFEST_NAME).write_bytes(encoded_manifest(manifest))


def verify_runtime(
    output_dir: Path,
    manifest: dict[str, object] | None = None,
    *,
    expected_catalog_hash: str | None = None,
    expected_runtime_version: str | None = None,
) -> dict[str, object]:
    runtime_root = ensure_runtime_child(output_dir)
    manifest_path = runtime_root / MANIFEST_NAME
    if not manifest_path.is_file():
        raise ValueError(f"runtime manifest is missing: {manifest_path}")
    try:
        actual_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"runtime manifest is invalid: {error}") from error
    if not isinstance(actual_manifest, dict):
        raise ValueError("runtime manifest root must be an object")
    if manifest is not None and actual_manifest != manifest:
        raise ValueError("runtime manifest does not match the pinned source tree")

    if actual_manifest.get("schema") != RUNTIME_SCHEMA:
        raise ValueError("runtime manifest schema is unsupported")
    if actual_manifest.get("api_version") != API_VERSION:
        raise ValueError("runtime manifest DLUMA API version is unsupported")
    actual_catalog_hash = actual_manifest.get("catalog_sha256")
    if (
        expected_catalog_hash is not None
        and actual_catalog_hash != expected_catalog_hash.upper()
    ):
        raise ValueError("runtime manifest DLUMA API catalog hash is stale")
    runtime = actual_manifest.get("runtime")
    if not isinstance(runtime, dict):
        raise ValueError("runtime manifest runtime record must be an object")
    if runtime.get("framework") != "Microsoft.NETCore.App":
        raise ValueError("runtime manifest framework is unsupported")
    if runtime.get("rid") != "win-x64":
        raise ValueError("runtime manifest RID is unsupported")
    if runtime.get("deployment") != "private-framework-dependent":
        raise ValueError("runtime manifest deployment is unsupported")
    if (
        expected_runtime_version is not None
        and runtime.get("version") != expected_runtime_version
    ):
        raise ValueError("runtime manifest framework version is unsupported")
    files = actual_manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError("runtime manifest files must be a non-empty array")

    expected_paths: set[str] = set()
    previous_key = ""
    for index, record in enumerate(files):
        if not isinstance(record, dict):
            raise ValueError(f"runtime manifest file {index} must be an object")
        relative = record.get("path")
        size = record.get("size")
        expected_hash = record.get("sha256")
        if not isinstance(relative, str):
            raise ValueError(f"runtime manifest file {index} has no path")
        path = PurePosixPath(relative)
        key = path.as_posix().casefold()
        if path.is_absolute() or ".." in path.parts or "\\" in relative:
            raise ValueError(f"runtime manifest contains unsafe path: {relative}")
        if not key or key == MANIFEST_NAME.casefold():
            raise ValueError(f"runtime manifest contains reserved path: {relative}")
        if key in expected_paths:
            raise ValueError(f"runtime manifest contains duplicate path: {relative}")
        if previous_key and key < previous_key:
            raise ValueError("runtime manifest file records are not sorted")
        previous_key = key
        expected_paths.add(key)
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise ValueError(f"runtime manifest has invalid size: {relative}")
        if (
            not isinstance(expected_hash, str)
            or len(expected_hash) != 64
            or any(
                character not in "0123456789abcdefABCDEF"
                for character in expected_hash
            )
        ):
            raise ValueError(f"runtime manifest has invalid SHA-256: {relative}")
        target = runtime_root.joinpath(*path.parts)
        if not target.is_file():
            raise ValueError(f"runtime tree file is missing: {relative}")
        if target.stat().st_size != size:
            raise ValueError(f"runtime tree file size changed: {relative}")
        if sha256_file(target) != expected_hash.upper():
            raise ValueError(f"runtime tree file SHA-256 changed: {relative}")

    actual_paths = {
        path.relative_to(runtime_root).as_posix().casefold()
        for path in runtime_root.rglob("*")
        if path.is_file() and path != manifest_path
    }
    manifest_runtime_version = runtime.get("version")
    if not isinstance(manifest_runtime_version, str) or not manifest_runtime_version:
        raise ValueError("runtime manifest framework version is invalid")
    required_paths = {
        (
            f"dotnet/host/fxr/{manifest_runtime_version}/hostfxr.dll"
        ).casefold(),
        (
            "dotnet/shared/Microsoft.NETCore.App/"
            f"{manifest_runtime_version}/coreclr.dll"
        ).casefold(),
        (
            "dotnet/shared/Microsoft.NETCore.App/"
            f"{manifest_runtime_version}/hostpolicy.dll"
        ).casefold(),
        (
            "dotnet/shared/Microsoft.NETCore.App/"
            f"{manifest_runtime_version}/System.Private.CoreLib.dll"
        ).casefold(),
        "dotnet/dotnet.exe".casefold(),
        (
            "dotnet/shared/Microsoft.WindowsDesktop.App/"
            f"{manifest_runtime_version}/PresentationFramework.dll"
        ).casefold(),
        "managed/Dluma.Managed.Bootstrap.dll".casefold(),
        "managed/Dluma.Managed.Bootstrap.deps.json".casefold(),
        "managed/Dluma.Managed.Bootstrap.runtimeconfig.json".casefold(),
        "managed/Dluma.Managed.Sdk.dll".casefold(),
        "licenses/dotnet/LICENSE.txt".casefold(),
        "licenses/dotnet/ThirdPartyNotices.txt".casefold(),
        *{
            (PurePosixPath("quest-graph") / artifact).as_posix().casefold()
            for artifact in QUEST_GRAPH_ARTIFACTS
        },
    }
    missing_required = sorted(required_paths - expected_paths)
    if missing_required:
        raise ValueError(
            f"runtime manifest omits required files: {missing_required}"
        )
    missing = sorted(expected_paths - actual_paths)
    unexpected = sorted(actual_paths - expected_paths)
    if missing or unexpected:
        raise ValueError(
            "runtime tree manifest mismatch; "
            f"missing={missing or '<none>'}, "
            f"unexpected={unexpected or '<none>'}"
        )
    return actual_manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--managed-output", required=True, type=Path)
    parser.add_argument("--quest-graph-output", required=True, type=Path)
    parser.add_argument("--dotnet-root", type=Path)
    parser.add_argument(
        "--catalog-hash",
        type=Path,
        default=Path("api/generated/dluma_api_catalog.sha256"),
    )
    parser.add_argument("--runtime-version", default=RUNTIME_VERSION)
    parser.add_argument("--loader-symbol-manifest", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    try:
        dotnet_root = resolve_dotnet_root(args.dotnet_root)
        sources = collect_sources(
            dotnet_root,
            args.managed_output,
            args.runtime_version,
            args.quest_graph_output,
            args.loader_symbol_manifest,
        )
        manifest = build_manifest(
            sources,
            args.runtime_version,
            read_catalog_hash(args.catalog_hash),
        )
        if args.check:
            verify_runtime(
                args.output_dir,
                manifest,
                expected_catalog_hash=manifest["catalog_sha256"],
                expected_runtime_version=args.runtime_version,
            )
            action = "Verified"
        else:
            write_runtime(args.output_dir, sources, manifest)
            verify_runtime(
                args.output_dir,
                manifest,
                expected_catalog_hash=manifest["catalog_sha256"],
                expected_runtime_version=args.runtime_version,
            )
            action = "Built and verified"
        total_size = sum(record["size"] for record in manifest["files"])
        print(
            f"{action} DLUMA private .NET {args.runtime_version} runtime: "
            f"{len(sources)} files, {total_size} bytes"
        )
        return 0
    except (OSError, ValueError) as error:
        print(f"managed runtime error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
