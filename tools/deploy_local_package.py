#!/usr/bin/env python3
"""Deploy and verify one exact DLUMA build package to local live roots."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import shutil
import sys


REJECTED_RUNTIME4_MOD_SHA256 = (
    "CBFC0D950EE2A4C4273AC3903A73A3AACED6BB39EB802F9F96ED544D6D5FD980"
)
TOP_LEVEL_PACKAGE_FILES = (
    "dluma.pak",
    "dluma_lua54.dll",
    "LICENSE_LUA_5.4.8.txt",
    "dluma_discord_presence.dll",
    "dluma_nightly_updater.exe",
)
PRESERVED_RUNTIME_DIAGNOSTIC_PATHS = (
    "dluma_runtime/dluma.log",
    "dluma_runtime/lua_discovery_report.txt",
    "dluma_runtime/custom_map_asset_trace.log",
    "dluma_runtime/custom_map_asset_trace_summary.txt",
    "dluma_runtime/custom_content_ledger_report.txt",
    "dluma_runtime/frontend_content_cold_reboot_report.txt",
    "dluma_runtime/frontend_startup_baseline.txt",
)
CRASH_REPORT_PATTERN = re.compile(
    r"^dluma_crash_session_[^/\\]+\.log$",
    re.IGNORECASE,
)
MAX_PRESERVED_RUNTIME_DIAGNOSTIC_FILES = 128
MAX_PRESERVED_RUNTIME_DIAGNOSTIC_BYTES = 64 * 1024 * 1024
MAX_TOTAL_PRESERVED_RUNTIME_DIAGNOSTIC_BYTES = 256 * 1024 * 1024
LOADER_MOD_PATTERN = re.compile(
    r"^DLUMA \(.+\)\.mod$",
    re.IGNORECASE,
)
ANIMATION_REFRESH_INI = "dluma.ini"
ANIMATION_REFRESH_DEFAULTS = ((b"Enabled", b"1"), (b"Route", b"ProjectRPack"))
_INI_SECTION = re.compile(rb"^[ \t]*\[([^\]\r\n]+)\][ \t]*(?:[;#].*)?$")
_INI_KEY = re.compile(rb"^[ \t]*([^=;#\[\]\r\n]+?)[ \t]*=")


class DeploymentError(RuntimeError):
    """A fail-closed package or destination verification error."""


@dataclass(frozen=True)
class PreservedRuntimeDiagnostic:
    content: bytes
    mtime_ns: int


@dataclass(frozen=True)
class DeploymentResult:
    mod_sha256: str
    pak_sha256: str
    subscribed_ini_manifest_sha256: str
    devtools_ini_manifest_sha256: str
    subscribed_runtime_diagnostic_manifest_sha256: str
    devtools_runtime_diagnostic_manifest_sha256: str
    subscribed_stale_mods_removed: tuple[str, ...]
    devtools_stale_mods_removed: tuple[str, ...]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as handle:
            while chunk := handle.read(1024 * 1024):
                digest.update(chunk)
    except OSError as error:
        raise DeploymentError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest().upper()


def _require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise DeploymentError(f"{label} is missing: {path}")


def _require_directory(path: Path, label: str) -> None:
    if not path.is_dir():
        raise DeploymentError(f"{label} is missing: {path}")


def _snapshot_ini_files(root: Path) -> dict[str, bytes]:
    snapshot: dict[str, bytes] = {}
    try:
        candidates = sorted(
            root.rglob("*"),
            key=lambda path: path.relative_to(root).as_posix().casefold(),
        )
        for path in candidates:
            if path.suffix.casefold() != ".ini" or not path.is_file():
                continue
            if path.is_symlink() or (
                hasattr(path, "is_junction") and path.is_junction()
            ):
                raise DeploymentError(
                    f"refusing to preserve linked INI path: {path}"
                )
            relative = path.relative_to(root).as_posix()
            snapshot[relative] = path.read_bytes()
    except OSError as error:
        raise DeploymentError(
            f"cannot capture installed INI files under {root}: {error}"
        ) from error
    return snapshot


def _migrate_animation_refresh_ini(content: bytes) -> bytes:
    """Add only missing animation-refresh defaults without rewriting existing lines."""
    if b"\0" in content:
        raise DeploymentError("loader INI uses an unsupported encoding")
    lines = content.splitlines(keepends=True)
    newline_match = re.search(rb"\r\n|\n|\r", content)
    newline = newline_match.group() if newline_match else b"\r\n"
    section_start: int | None = None
    section_end = len(lines)
    present: set[bytes] = set()
    in_section = False
    for index, line in enumerate(lines):
        body = line.rstrip(b"\r\n")
        if index == 0 and body.startswith(b"\xef\xbb\xbf"):
            body = body[3:]
        stripped = body.strip()
        if not stripped or stripped.startswith((b";", b"#")):
            continue
        header = _INI_SECTION.fullmatch(body)
        if header:
            if in_section:
                section_end = index
                in_section = False
            if header.group(1).strip().lower() == b"animationrefresh":
                if section_start is not None:
                    raise DeploymentError("loader INI has duplicate AnimationRefresh sections")
                section_start = index
                in_section = True
            continue
        if stripped.startswith(b"["):
            if in_section or b"animationrefresh" in stripped.lower():
                raise DeploymentError("loader INI has an ambiguous section header")
            continue
        if not in_section:
            continue
        key_match = _INI_KEY.match(body)
        key = key_match.group(1).strip().lower() if key_match else b""
        if key in (b"enabled", b"route"):
            if key in present:
                raise DeploymentError("loader INI has duplicate AnimationRefresh keys")
            present.add(key)
        elif stripped.split(None, 1)[0].lower() in (b"enabled", b"route"):
            raise DeploymentError("loader INI has an ambiguous AnimationRefresh key")

    missing = [
        key + b"=" + value + newline
        for key, value in ANIMATION_REFRESH_DEFAULTS
        if key.lower() not in present
    ]
    if not missing:
        return content
    if section_start is None:
        separator = b"" if content in (b"", b"\xef\xbb\xbf") else (
            newline if content.endswith((b"\r", b"\n")) else newline + newline
        )
        return content + separator + b"[AnimationRefresh]" + newline + b"".join(missing)
    prefix = b"".join(lines[:section_end])
    if prefix and not prefix.endswith((b"\r", b"\n")):
        prefix += newline
    return prefix + b"".join(missing) + b"".join(lines[section_end:])


def _expected_ini_after_animation_refresh(
    snapshot: dict[str, bytes],
) -> dict[str, bytes]:
    expected = snapshot.copy()
    matches = [
        relative for relative in expected
        if relative.casefold() == ANIMATION_REFRESH_INI
    ]
    if len(matches) > 1:
        raise DeploymentError("loader INI has ambiguous file casing")
    relative = matches[0] if matches else ANIMATION_REFRESH_INI
    expected[relative] = _migrate_animation_refresh_ini(expected.get(relative, b""))
    return expected


def _is_linked_path(path: Path) -> bool:
    return path.is_symlink() or (
        hasattr(path, "is_junction") and path.is_junction()
    )


def _require_unlinked_parent_chain(root: Path, path: Path, label: str) -> None:
    relative = path.relative_to(root)
    current = root
    for part in relative.parts:
        current /= part
        if current.exists() and _is_linked_path(current):
            raise DeploymentError(f"refusing linked {label} path: {current}")


def _snapshot_runtime_diagnostics(
    root: Path,
) -> dict[str, PreservedRuntimeDiagnostic]:
    snapshot: dict[str, PreservedRuntimeDiagnostic] = {}
    runtime_dir = root / "dluma_runtime"
    crash_dir = runtime_dir / "crashes"
    try:
        if runtime_dir.exists():
            _require_unlinked_parent_chain(
                root, runtime_dir, "runtime diagnostic"
            )

        candidates = [
            root.joinpath(*relative.split("/"))
            for relative in PRESERVED_RUNTIME_DIAGNOSTIC_PATHS
        ]
        if crash_dir.exists():
            _require_unlinked_parent_chain(
                root, crash_dir, "runtime diagnostic"
            )
            if not crash_dir.is_dir():
                raise DeploymentError(
                    f"runtime crash-report path is not a directory: "
                    f"{crash_dir}"
                )
            candidates.extend(
                path
                for path in crash_dir.iterdir()
                if CRASH_REPORT_PATTERN.fullmatch(path.name)
            )

        candidates = sorted(
            (path for path in candidates if path.exists()),
            key=lambda path: path.relative_to(root).as_posix().casefold(),
        )
        if len(candidates) > MAX_PRESERVED_RUNTIME_DIAGNOSTIC_FILES:
            raise DeploymentError(
                "too many runtime diagnostic files to preserve under "
                f"{root}: {len(candidates)} > "
                f"{MAX_PRESERVED_RUNTIME_DIAGNOSTIC_FILES}"
            )

        total_size = 0
        for path in candidates:
            if _is_linked_path(path) or not path.is_file():
                raise DeploymentError(
                    f"refusing non-regular runtime diagnostic path: {path}"
                )
            before = path.stat()
            if before.st_size > MAX_PRESERVED_RUNTIME_DIAGNOSTIC_BYTES:
                raise DeploymentError(
                    f"runtime diagnostic is too large to preserve: {path} "
                    f"({before.st_size} bytes)"
                )
            total_size += before.st_size
            if total_size > MAX_TOTAL_PRESERVED_RUNTIME_DIAGNOSTIC_BYTES:
                raise DeploymentError(
                    "runtime diagnostics exceed the preservation limit under "
                    f"{root}: {total_size} bytes"
                )
            content = path.read_bytes()
            after = path.stat()
            if (
                before.st_size != after.st_size
                or before.st_mtime_ns != after.st_mtime_ns
                or before.st_ino != after.st_ino
                or len(content) != after.st_size
            ):
                raise DeploymentError(
                    f"runtime diagnostic changed while being preserved: {path}"
                )
            relative = path.relative_to(root).as_posix()
            snapshot[relative] = PreservedRuntimeDiagnostic(
                content=content,
                mtime_ns=after.st_mtime_ns,
            )
    except OSError as error:
        raise DeploymentError(
            f"cannot capture runtime diagnostics under {root}: {error}"
        ) from error
    return snapshot


def _ini_manifest_sha256(snapshot: dict[str, bytes]) -> str:
    digest = hashlib.sha256()
    for relative, content in sorted(
        snapshot.items(), key=lambda item: item[0].casefold()
    ):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(content).digest())
        digest.update(b"\0")
    return digest.hexdigest().upper()


def _runtime_diagnostic_manifest_sha256(
    snapshot: dict[str, PreservedRuntimeDiagnostic],
) -> str:
    digest = hashlib.sha256()
    for relative, record in sorted(
        snapshot.items(), key=lambda item: item[0].casefold()
    ):
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(hashlib.sha256(record.content).digest())
        digest.update(b"\0")
    return digest.hexdigest().upper()


def _atomic_copy(source: Path, destination: Path) -> None:
    temporary = destination.with_name(
        f"{destination.name}.dluma-deploy-{os.getpid()}.tmp"
    )
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)
    except OSError as error:
        raise DeploymentError(
            f"cannot copy {source} to {destination}: {error}"
        ) from error
    finally:
        try:
            temporary.unlink(missing_ok=True)
        except OSError:
            pass


def _restore_ini_files(root: Path, snapshot: dict[str, bytes]) -> None:
    for relative, content in snapshot.items():
        destination = root.joinpath(*relative.split("/"))
        try:
            if destination.is_file() and destination.read_bytes() == content:
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(
                f"{destination.name}.dluma-ini-{os.getpid()}.tmp"
            )
            try:
                temporary.write_bytes(content)
                os.replace(temporary, destination)
            finally:
                temporary.unlink(missing_ok=True)
        except OSError as error:
            raise DeploymentError(
                f"cannot restore preserved INI {destination}: {error}"
            ) from error


def _restore_runtime_diagnostics(
    root: Path,
    snapshot: dict[str, PreservedRuntimeDiagnostic],
) -> None:
    for relative, record in snapshot.items():
        destination = root.joinpath(*relative.split("/"))
        try:
            _require_unlinked_parent_chain(
                root, destination.parent, "runtime diagnostic"
            )
            if destination.exists() and _is_linked_path(destination):
                raise DeploymentError(
                    f"refusing linked runtime diagnostic path: {destination}"
                )
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(
                f"{destination.name}.dluma-diagnostic-{os.getpid()}.tmp"
            )
            try:
                temporary.write_bytes(record.content)
                os.replace(temporary, destination)
                os.utime(
                    destination,
                    ns=(record.mtime_ns, record.mtime_ns),
                )
            finally:
                temporary.unlink(missing_ok=True)
        except DeploymentError:
            raise
        except OSError as error:
            raise DeploymentError(
                f"cannot restore runtime diagnostic {destination}: {error}"
            ) from error


def _versioned_loader_mods(root: Path) -> list[Path]:
    try:
        return sorted(
            (
                path
                for path in root.iterdir()
                if path.is_file() and LOADER_MOD_PATTERN.fullmatch(path.name)
            ),
            key=lambda path: path.name.casefold(),
        )
    except OSError as error:
        raise DeploymentError(
            f"cannot enumerate loader MODs under {root}: {error}"
        ) from error


def _validate_source_artifacts(
    package_dir: Path,
    built_mod: Path,
    built_pak: Path,
    mod_name: str,
) -> tuple[str, str]:
    package_mod = package_dir / mod_name
    package_pak = package_dir / "dluma.pak"

    _require_file(built_mod, "built MOD")
    _require_file(built_pak, "built PAK")
    _require_file(package_mod, "package MOD")
    _require_file(package_pak, "package PAK")
    for name in TOP_LEVEL_PACKAGE_FILES:
        _require_file(package_dir / name, f"package file {name}")
    runtime_dir = package_dir / "dluma_runtime"
    _require_directory(runtime_dir, "package private runtime")
    if not any(path.is_file() for path in runtime_dir.rglob("*")):
        raise DeploymentError(
            f"package private runtime contains no files: {runtime_dir}"
        )
    packaged_diagnostics = _snapshot_runtime_diagnostics(package_dir)
    if packaged_diagnostics:
        raise DeploymentError(
            "package private runtime must not contain generated diagnostics: "
            f"{sorted(packaged_diagnostics)}"
        )

    mod_hash = sha256_file(built_mod)
    mod_sources = {
        "built MOD": mod_hash,
        "package MOD": sha256_file(package_mod),
    }
    if len(set(mod_sources.values())) != 1:
        raise DeploymentError(f"source MOD hash mismatch: {mod_sources}")

    pak_hash = sha256_file(built_pak)
    pak_sources = {
        "built PAK": pak_hash,
        "package PAK": sha256_file(package_pak),
    }
    if len(set(pak_sources.values())) != 1:
        raise DeploymentError(f"source PAK hash mismatch: {pak_sources}")
    return mod_hash, pak_hash


def _deploy_destination(
    package_dir: Path,
    destination: Path,
    mod_name: str,
    expected_ini: dict[str, bytes],
    preserved_runtime_diagnostics: dict[str, PreservedRuntimeDiagnostic],
) -> tuple[str, ...]:
    _require_directory(destination, "live deployment destination")
    removed: list[str] = []
    for stale_mod in _versioned_loader_mods(destination):
        if stale_mod.name.casefold() == mod_name.casefold():
            continue
        try:
            stale_mod.unlink()
        except OSError as error:
            raise DeploymentError(
                f"cannot remove stale loader MOD {stale_mod}: {error}"
            ) from error
        removed.append(stale_mod.name)

    runtime_destination = destination / "dluma_runtime"
    try:
        if runtime_destination.exists():
            shutil.rmtree(runtime_destination)
    except OSError as error:
        raise DeploymentError(
            f"cannot remove old private runtime {runtime_destination}: {error}"
        ) from error

    _atomic_copy(package_dir / mod_name, destination / mod_name)
    for name in TOP_LEVEL_PACKAGE_FILES:
        _atomic_copy(package_dir / name, destination / name)

    try:
        shutil.copytree(package_dir / "dluma_runtime", runtime_destination)
    except OSError as error:
        raise DeploymentError(
            f"cannot copy private runtime to {runtime_destination}: {error}"
        ) from error
    _restore_ini_files(destination, expected_ini)
    _restore_runtime_diagnostics(destination, preserved_runtime_diagnostics)
    return tuple(removed)


def verify_deployment(
    *,
    subscribed_dir: Path,
    devtools_dir: Path,
    built_mod: Path,
    built_pak: Path,
    mod_name: str,
    rejected_mod_sha256: str = REJECTED_RUNTIME4_MOD_SHA256,
    expected_ini: dict[Path, dict[str, bytes]] | None = None,
    expected_runtime_diagnostics: (
        dict[Path, dict[str, PreservedRuntimeDiagnostic]] | None
    ) = None,
) -> tuple[str, str]:
    mod_paths = {
        "built": built_mod,
        "subscribed": subscribed_dir / mod_name,
        "DevTools": devtools_dir / mod_name,
    }
    pak_paths = {
        "built": built_pak,
        "subscribed": subscribed_dir / "dluma.pak",
        "DevTools": devtools_dir / "dluma.pak",
    }
    mod_hashes = {label: sha256_file(path) for label, path in mod_paths.items()}
    pak_hashes = {label: sha256_file(path) for label, path in pak_paths.items()}
    if len(set(mod_hashes.values())) != 1:
        raise DeploymentError(f"deployed MOD hash mismatch: {mod_hashes}")
    if len(set(pak_hashes.values())) != 1:
        raise DeploymentError(f"deployed PAK hash mismatch: {pak_hashes}")

    rejected = rejected_mod_sha256.upper()
    for label in ("subscribed", "DevTools"):
        if mod_hashes[label] == rejected:
            raise DeploymentError(
                f"{label} installed MOD still equals rejected stale SHA-256 "
                f"{rejected}"
            )

    for label, root in (
        ("subscribed", subscribed_dir),
        ("DevTools", devtools_dir),
    ):
        installed_mods = _versioned_loader_mods(root)
        if len(installed_mods) != 1 or (
            installed_mods[0].name.casefold() != mod_name.casefold()
        ):
            names = [path.name for path in installed_mods]
            raise DeploymentError(
                f"{label} destination must contain exactly {mod_name!r}; "
                f"found {names}"
            )

    if expected_ini is not None:
        for root in (subscribed_dir, devtools_dir):
            before = expected_ini[root]
            after = _snapshot_ini_files(root)
            if after != before:
                raise DeploymentError(
                    f"installed INI files changed under {root}: "
                    f"before={sorted(before)} after={sorted(after)}"
                )
    if expected_runtime_diagnostics is not None:
        for root in (subscribed_dir, devtools_dir):
            before = expected_runtime_diagnostics[root]
            after = _snapshot_runtime_diagnostics(root)
            if after != before:
                raise DeploymentError(
                    f"installed runtime diagnostics changed under {root}: "
                    f"before={sorted(before)} after={sorted(after)}"
                )
    return next(iter(mod_hashes.values())), next(iter(pak_hashes.values()))


def deploy_local_package(
    *,
    package_dir: Path,
    subscribed_dir: Path,
    devtools_dir: Path,
    built_mod: Path,
    built_pak: Path,
    mod_name: str,
    rejected_mod_sha256: str = REJECTED_RUNTIME4_MOD_SHA256,
) -> DeploymentResult:
    package_dir = package_dir.resolve()
    subscribed_dir = subscribed_dir.resolve()
    devtools_dir = devtools_dir.resolve()
    built_mod = built_mod.resolve()
    built_pak = built_pak.resolve()
    if subscribed_dir == devtools_dir:
        raise DeploymentError("the two live deployment destinations are identical")

    _validate_source_artifacts(package_dir, built_mod, built_pak, mod_name)
    ini_before = {
        subscribed_dir: _snapshot_ini_files(subscribed_dir),
        devtools_dir: _snapshot_ini_files(devtools_dir),
    }
    ini_expected = {
        root: _expected_ini_after_animation_refresh(snapshot)
        for root, snapshot in ini_before.items()
    }
    runtime_diagnostics_before = {
        subscribed_dir: _snapshot_runtime_diagnostics(subscribed_dir),
        devtools_dir: _snapshot_runtime_diagnostics(devtools_dir),
    }
    subscribed_removed = _deploy_destination(
        package_dir,
        subscribed_dir,
        mod_name,
        ini_expected[subscribed_dir],
        runtime_diagnostics_before[subscribed_dir],
    )
    devtools_removed = _deploy_destination(
        package_dir,
        devtools_dir,
        mod_name,
        ini_expected[devtools_dir],
        runtime_diagnostics_before[devtools_dir],
    )
    mod_hash, pak_hash = verify_deployment(
        subscribed_dir=subscribed_dir,
        devtools_dir=devtools_dir,
        built_mod=built_mod,
        built_pak=built_pak,
        mod_name=mod_name,
        rejected_mod_sha256=rejected_mod_sha256,
        expected_ini=ini_expected,
        expected_runtime_diagnostics=runtime_diagnostics_before,
    )
    return DeploymentResult(
        mod_sha256=mod_hash,
        pak_sha256=pak_hash,
        subscribed_ini_manifest_sha256=_ini_manifest_sha256(
            ini_expected[subscribed_dir]
        ),
        devtools_ini_manifest_sha256=_ini_manifest_sha256(
            ini_expected[devtools_dir]
        ),
        subscribed_runtime_diagnostic_manifest_sha256=(
            _runtime_diagnostic_manifest_sha256(
                runtime_diagnostics_before[subscribed_dir]
            )
        ),
        devtools_runtime_diagnostic_manifest_sha256=(
            _runtime_diagnostic_manifest_sha256(
                runtime_diagnostics_before[devtools_dir]
            )
        ),
        subscribed_stale_mods_removed=subscribed_removed,
        devtools_stale_mods_removed=devtools_removed,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--subscribed-dir", type=Path, required=True)
    parser.add_argument("--devtools-dir", type=Path, required=True)
    parser.add_argument("--built-mod", type=Path, required=True)
    parser.add_argument("--built-pak", type=Path, required=True)
    parser.add_argument("--mod-name", required=True)
    parser.add_argument(
        "--reject-mod-sha256",
        default=REJECTED_RUNTIME4_MOD_SHA256,
    )
    args = parser.parse_args()
    try:
        result = deploy_local_package(
            package_dir=args.package_dir,
            subscribed_dir=args.subscribed_dir,
            devtools_dir=args.devtools_dir,
            built_mod=args.built_mod,
            built_pak=args.built_pak,
            mod_name=args.mod_name,
            rejected_mod_sha256=args.reject_mod_sha256,
        )
    except DeploymentError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(
        "DLUMA LOCAL DEPLOYMENT VERIFIED "
        f"MOD_SHA256={result.mod_sha256} "
        f"PAK_SHA256={result.pak_sha256}"
    )
    print(
        f"DLUMA SUBSCRIBED DESTINATION={args.subscribed_dir.resolve()} "
        f"MOD_SHA256={result.mod_sha256} "
        f"PAK_SHA256={result.pak_sha256} "
        "INI_MANIFEST_SHA256="
        f"{result.subscribed_ini_manifest_sha256} "
        "RUNTIME_DIAGNOSTIC_MANIFEST_SHA256="
        f"{result.subscribed_runtime_diagnostic_manifest_sha256} "
        "STALE_MODS_REMOVED="
        f"{list(result.subscribed_stale_mods_removed)}"
    )
    print(
        f"DLUMA DEVTOOLS DESTINATION={args.devtools_dir.resolve()} "
        f"MOD_SHA256={result.mod_sha256} "
        f"PAK_SHA256={result.pak_sha256} "
        f"INI_MANIFEST_SHA256={result.devtools_ini_manifest_sha256} "
        "RUNTIME_DIAGNOSTIC_MANIFEST_SHA256="
        f"{result.devtools_runtime_diagnostic_manifest_sha256} "
        "STALE_MODS_REMOVED="
        f"{list(result.devtools_stale_mods_removed)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
