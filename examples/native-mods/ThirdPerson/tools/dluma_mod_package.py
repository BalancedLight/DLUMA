"""Shared DLUMA API 1.0 manifest, lock, and deterministic package support."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
CATALOG_PATH = ROOT / "sdk" / "dluma_api_catalog.json"
LOCK_NAME = "mod.dluma.lock.json"
MANIFEST_NAME = "mod.dluma.json"
MAX_MANIFEST_BYTES = 64 * 1024
MAX_LOCK_BYTES = 1024 * 1024
MAX_ARTIFACT_BYTES = 1024 * 1024 * 1024
MAX_ARTIFACTS = 1024
REPARSE_ATTRIBUTE = 0x400
IMMEDIATELY_PREVIOUS_CATALOG_SHA256 = (
    "D49EDB1807E5E3B0109AF54D049D95E07072F499F480DD878F32AEE810DA220E"
)

ID_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]*[a-z0-9]$")
VERSION_PATTERN = re.compile(
    r"^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$"
)
ACCESS_PATTERN = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$")
QUEST_PHASE_HANDLER_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
QUEST_PHASE_ALIAS_PATTERN = re.compile(r"^[a-z0-9][a-z0-9_-]*$")
ENTRY_TYPE_PATTERN = re.compile(r"^[A-Za-z_][A-Za-z0-9_.+`]*$")
SHA256_PATTERN = re.compile(r"^[A-F0-9]{64}$")

DEVICE_NAMES = {
    "CON",
    "PRN",
    "AUX",
    "NUL",
    *(f"COM{index}" for index in range(1, 10)),
    *(f"LPT{index}" for index in range(1, 10)),
}

COMMON_KEYS = {
    "$schema",
    "schema",
    "api_version",
    "id",
    "name",
    "version",
    "description",
    "author",
    "language",
    "scope",
    "map",
    "entry",
    "entry_type",
    "load_priority",
    "access",
    "hosts",
    "quest_phase_handlers",
    "configuration",
    "companion_content",
    "storage_migration",
}
REQUIRED_KEYS = {
    "schema",
    "api_version",
    "id",
    "name",
    "version",
    "description",
    "author",
    "language",
    "scope",
    "entry",
    "load_priority",
    "access",
}


class PackageError(ValueError):
    pass


@dataclass(frozen=True)
class Artifact:
    path: str
    kind: str
    size: int
    sha256: str

    def as_json(self) -> dict[str, Any]:
        return {
            "path": self.path,
            "kind": self.kind,
            "size": self.size,
            "sha256": self.sha256,
        }


def _reject_duplicate_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise PackageError(f"duplicate JSON property: {key}")
        result[key] = value
    return result


def load_json_exact(path: Path, maximum: int) -> tuple[dict[str, Any], bytes]:
    try:
        before = path.stat()
    except OSError as exc:
        raise PackageError(f"cannot stat {path}: {exc}") from exc
    if not stat.S_ISREG(before.st_mode):
        raise PackageError(f"not a regular file: {path}")
    if before.st_size > maximum:
        raise PackageError(f"file exceeds {maximum} bytes: {path}")
    _reject_unsafe_file_identity(path, before)
    try:
        raw = path.read_bytes()
        after = path.stat()
    except OSError as exc:
        raise PackageError(f"cannot read {path}: {exc}") from exc
    if _identity(before) != _identity(after) or len(raw) != before.st_size:
        raise PackageError(f"file changed while it was being read: {path}")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_object,
            parse_constant=lambda value: (_ for _ in ()).throw(
                PackageError(f"non-finite JSON value: {value}")
            ),
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise PackageError(f"invalid UTF-8 JSON in {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise PackageError(f"JSON root must be an object: {path}")
    return value, raw


def _identity(value: os.stat_result) -> tuple[int, int, int, int, int]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_size,
        value.st_mtime_ns,
        value.st_nlink,
    )


def _reject_unsafe_file_identity(path: Path, value: os.stat_result) -> None:
    attributes = getattr(value, "st_file_attributes", 0)
    if path.is_symlink() or attributes & REPARSE_ATTRIBUTE:
        raise PackageError(f"reparse points and symlinks are not allowed: {path}")
    if value.st_nlink != 1:
        raise PackageError(f"hard-linked artifacts are not allowed: {path}")


def canonical_relative_path(value: str) -> str:
    if not isinstance(value, str) or not value or "\x00" in value:
        raise PackageError("relative path must be a non-empty NUL-free string")
    normalized = value.replace("\\", "/")
    if re.match(r"^[A-Za-z]:", normalized) or normalized.startswith("/"):
        raise PackageError(f"absolute paths are not allowed: {value!r}")
    if "//" in normalized:
        raise PackageError(f"empty path segments are not allowed: {value!r}")
    path = PurePosixPath(normalized)
    if any(part in {"", ".", ".."} for part in path.parts):
        raise PackageError(f"ambiguous path segments are not allowed: {value!r}")
    for part in path.parts:
        if part.endswith((" ", ".")):
            raise PackageError(f"trailing dot/space path aliases are not allowed: {value!r}")
        stem = part.split(".", 1)[0].upper()
        if stem in DEVICE_NAMES:
            raise PackageError(f"Windows device path is not allowed: {value!r}")
        if any(character in part for character in '<>:"|?*'):
            raise PackageError(f"invalid Windows path character: {value!r}")
    encoded = normalized.encode("utf-8")
    if len(encoded) > 511:
        raise PackageError("relative path exceeds 511 UTF-8 bytes")
    return normalized


def _bounded_text(
    manifest: dict[str, Any],
    key: str,
    minimum: int,
    maximum: int,
) -> str:
    value = manifest.get(key)
    if not isinstance(value, str):
        raise PackageError(f"{key} must be a string")
    length = len(value.encode("utf-8"))
    if length < minimum or length > maximum or "\x00" in value:
        raise PackageError(
            f"{key} must contain {minimum}..{maximum} UTF-8 bytes"
        )
    return value


def load_catalog_access() -> tuple[str, set[str]]:
    catalog, _ = load_json_exact(CATALOG_PATH, 4 * 1024 * 1024)
    digest = catalog.get("catalog_sha256")
    if not isinstance(digest, str) or not SHA256_PATTERN.fullmatch(digest):
        raise PackageError("generated API catalog has an invalid hash")
    access = {
        item["name"]
        for item in catalog.get("access", [])
        if isinstance(item, dict) and isinstance(item.get("name"), str)
    }
    return digest, access


def validate_manifest(mod_dir: Path) -> tuple[dict[str, Any], bytes]:
    mod_dir = mod_dir.resolve(strict=True)
    if not mod_dir.is_dir():
        raise PackageError(f"mod directory does not exist: {mod_dir}")
    manifest, raw = load_json_exact(mod_dir / MANIFEST_NAME, MAX_MANIFEST_BYTES)
    unknown = set(manifest) - COMMON_KEYS
    missing = REQUIRED_KEYS - set(manifest)
    if unknown:
        raise PackageError(f"unknown manifest properties: {sorted(unknown)}")
    if missing:
        raise PackageError(f"missing manifest properties: {sorted(missing)}")
    if manifest["schema"] != "dluma.mod.v1":
        raise PackageError("schema must be dluma.mod.v1")
    if manifest["api_version"] != "1.0":
        raise PackageError("api_version must be 1.0")

    mod_id = _bounded_text(manifest, "id", 3, 127)
    if not ID_PATTERN.fullmatch(mod_id):
        raise PackageError("id must be lowercase and use only letters, digits, ._-")
    _bounded_text(manifest, "name", 1, 127)
    version = _bounded_text(manifest, "version", 1, 63)
    if not VERSION_PATTERN.fullmatch(version):
        raise PackageError("version must be a three-part semantic version")
    _bounded_text(manifest, "description", 0, 1024)
    _bounded_text(manifest, "author", 1, 127)

    language = manifest.get("language")
    if language not in {"lua", "csharp", "native"}:
        raise PackageError("language must be lua, csharp, or native")
    scope = manifest.get("scope")
    if scope not in {"global", "project", "map"}:
        raise PackageError("scope must be global, project, or map")
    map_name = manifest.get("map")
    if scope == "map":
        if not isinstance(map_name, str) or not map_name:
            raise PackageError("map scope requires a map property")
        _validate_folder_name(map_name, "map")
    elif "map" in manifest:
        raise PackageError("map is valid only for map scope")

    entry_type = manifest.get("entry_type")
    if language == "csharp":
        if not isinstance(entry_type, str) or not ENTRY_TYPE_PATTERN.fullmatch(
            entry_type
        ):
            raise PackageError("C# mods require a valid entry_type")
    elif "entry_type" in manifest:
        raise PackageError("entry_type is valid only for C# mods")

    priority = manifest.get("load_priority")
    if (
        isinstance(priority, bool)
        or not isinstance(priority, int)
        or not -1000 <= priority <= 1000
    ):
        raise PackageError("load_priority must be an integer from -1000 to 1000")

    _, known_access = load_catalog_access()
    access = manifest.get("access")
    if not isinstance(access, list) or len(access) > 64:
        raise PackageError("access must be an array with at most 64 entries")
    if len(access) != len(set(access)):
        raise PackageError("access declarations must be unique")
    for item in access:
        if (
            not isinstance(item, str)
            or not ACCESS_PATTERN.fullmatch(item)
            or item not in known_access
        ):
            raise PackageError(f"unknown access declaration: {item!r}")

    hosts = manifest.get("hosts", ["player"])
    if (
        not isinstance(hosts, list)
        or not 1 <= len(hosts) <= 2
        or len(hosts) != len(set(hosts))
        or any(host not in {"player", "editor"} for host in hosts)
    ):
        raise PackageError("hosts must be a unique non-empty subset of player/editor")
    if "editor" in hosts and language != "native":
        raise PackageError("only native mods can execute inside Chrome Editor")

    handlers = manifest.get("quest_phase_handlers", [])
    if not isinstance(handlers, list) or len(handlers) > 32:
        raise PackageError("quest_phase_handlers must contain at most 32 entries")
    if handlers and language == "csharp":
        raise PackageError("C# quest phase handlers are unavailable")
    if handlers and "player" not in hosts:
        raise PackageError("quest_phase_handlers require the player host")
    if handlers and "quest.phase.handle" not in access:
        raise PackageError("quest_phase_handlers require quest.phase.handle")
    handler_ids: set[str] = set()
    aliases: set[str] = set()
    for item in handlers:
        if (
            not isinstance(item, dict)
            or set(item) - {"id", "command_alias", "description"}
            or not {"id", "description"} <= set(item)
        ):
            raise PackageError("invalid quest_phase_handlers entry")
        handler_id = item.get("id")
        if (
            not isinstance(handler_id, str)
            or not 1 <= len(handler_id.encode("utf-8")) <= 127
            or not QUEST_PHASE_HANDLER_PATTERN.fullmatch(handler_id)
            or handler_id in handler_ids
        ):
            raise PackageError("quest phase handler IDs must be unique lowercase IDs")
        handler_ids.add(handler_id)
        description = item.get("description")
        if (
            not isinstance(description, str)
            or len(description.encode("utf-8")) > 255
            or "\x00" in description
        ):
            raise PackageError("quest phase handler description exceeds 255 UTF-8 bytes")
        alias = item.get("command_alias")
        if alias is not None:
            folded = alias.casefold() if isinstance(alias, str) else ""
            if (
                not isinstance(alias, str)
                or not QUEST_PHASE_ALIAS_PATTERN.fullmatch(alias)
                or len(alias.encode("utf-8")) > 63
                or folded in aliases
            ):
                raise PackageError("quest phase command aliases must be unique lowercase aliases")
            aliases.add(folded)

    entry = canonical_relative_path(manifest["entry"])
    suffixes = {
        "lua": {".lua"},
        "csharp": {".dll"},
        "native": {".mod", ".dll"},
    }
    if PurePosixPath(entry).suffix.lower() not in suffixes[language]:
        raise PackageError(f"{language} entry has an unsupported extension")
    _resolve_artifact(mod_dir, entry)

    configuration = manifest.get("configuration")
    if configuration is not None:
        if not isinstance(configuration, dict):
            raise PackageError("configuration must be an object")
        unknown_config = set(configuration) - {"schema", "defaults"}
        if unknown_config or "schema" not in configuration:
            raise PackageError("configuration requires schema and optional defaults")
        for key in ("schema", "defaults"):
            if key in configuration:
                _resolve_artifact(
                    mod_dir, canonical_relative_path(configuration[key])
                )

    companion = manifest.get("companion_content", [])
    if not isinstance(companion, list) or len(companion) > 16:
        raise PackageError("companion_content must contain at most 16 entries")
    companion_ids: set[str] = set()
    for item in companion:
        if not isinstance(item, dict) or set(item) - {
            "id", "path", "audio_manifest", "required"
        }:
            raise PackageError("invalid companion_content entry")
        companion_id = item.get("id")
        if (
            not isinstance(companion_id, str)
            or not companion_id
            or companion_id in companion_ids
            or not re.fullmatch(r"^[a-z0-9][a-z0-9._-]*$", companion_id)
        ):
            raise PackageError("companion content IDs must be unique lowercase IDs")
        companion_ids.add(companion_id)
        if "required" in item and not isinstance(item["required"], bool):
            raise PackageError("companion_content.required must be boolean")
        if "audio_manifest" in item:
            canonical_relative_path(item["audio_manifest"])
        _resolve_artifact(mod_dir, canonical_relative_path(item.get("path")))

    migration = manifest.get("storage_migration")
    if migration is not None:
        if (
            not isinstance(migration, dict)
            or set(migration) - {"legacy_id", "legacy_fingerprint"}
            or "legacy_id" not in migration
        ):
            raise PackageError("invalid storage_migration declaration")
        legacy_id = migration["legacy_id"]
        if not isinstance(legacy_id, str) or not 3 <= len(legacy_id) <= 127:
            raise PackageError("storage_migration.legacy_id is invalid")
        fingerprint = migration.get("legacy_fingerprint")
        if fingerprint is not None and (
            not isinstance(fingerprint, str)
            or not SHA256_PATTERN.fullmatch(fingerprint)
        ):
            raise PackageError("storage_migration.legacy_fingerprint is invalid")

    validate_folder_layout(mod_dir, manifest)
    return manifest, raw


def _validate_folder_name(value: str, label: str) -> None:
    if value in {".", ".."} or value.endswith((" ", ".")):
        raise PackageError(f"{label} contains an ambiguous folder name")
    if any(character in value for character in '\\/:*?"<>|\x00'):
        raise PackageError(f"{label} contains an invalid folder character")
    if value.split(".", 1)[0].upper() in DEVICE_NAMES:
        raise PackageError(f"{label} uses a Windows device name")
    if len(value.encode("utf-8")) > 127:
        raise PackageError(f"{label} exceeds 127 UTF-8 bytes")


def validate_folder_layout(mod_dir: Path, manifest: dict[str, Any]) -> None:
    base = {
        "lua": ("data", "scripts", "lua"),
        "csharp": ("mods", "csharp"),
        "native": ("mods", "native"),
    }[manifest["language"]]
    if manifest["scope"] == "map":
        expected = (*base, "maps", manifest["map"], manifest["id"])
    else:
        expected = (*base, manifest["scope"], manifest["id"])
    actual = tuple(part.casefold() for part in mod_dir.parts[-len(expected) :])
    wanted = tuple(part.casefold() for part in expected)
    if actual != wanted:
        expected_text = "\\".join(expected)
        raise PackageError(
            f"folder-derived language/scope must end with {expected_text}"
        )


def _resolve_artifact(mod_dir: Path, relative: str) -> Path:
    candidate = mod_dir.joinpath(*PurePosixPath(relative).parts)
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as exc:
        raise PackageError(f"missing artifact {relative}: {exc}") from exc
    try:
        resolved.relative_to(mod_dir)
    except ValueError as exc:
        raise PackageError(f"artifact escapes the mod directory: {relative}") from exc
    value = resolved.stat()
    if not stat.S_ISREG(value.st_mode):
        raise PackageError(f"artifact is not a regular file: {relative}")
    _reject_unsafe_file_identity(candidate, value)
    return resolved


def _classify_artifact(relative: str, manifest: dict[str, Any]) -> str:
    if relative == MANIFEST_NAME:
        return "manifest"
    if relative == manifest["entry"].replace("\\", "/"):
        return "entry"
    suffix = PurePosixPath(relative).suffix.lower()
    name = PurePosixPath(relative).name.lower()
    if suffix == ".lua":
        return "script_module"
    if suffix == ".dll":
        return "assembly" if manifest["language"] == "csharp" else "dependency"
    if suffix in {".json", ".ini"}:
        return "configuration"
    if suffix == ".pak":
        return "companion"
    if name.startswith(("license", "copying", "notice")):
        return "license"
    return "dependency"


def _read_artifact(mod_dir: Path, path: Path, manifest: dict[str, Any]) -> Artifact:
    relative = path.relative_to(mod_dir).as_posix()
    canonical_relative_path(relative)
    before = path.stat()
    _reject_unsafe_file_identity(path, before)
    if before.st_size > MAX_ARTIFACT_BYTES:
        raise PackageError(f"artifact exceeds {MAX_ARTIFACT_BYTES} bytes: {relative}")
    raw = path.read_bytes()
    after = path.stat()
    if _identity(before) != _identity(after) or len(raw) != before.st_size:
        raise PackageError(f"artifact changed while hashing: {relative}")
    return Artifact(
        path=relative,
        kind=_classify_artifact(relative, manifest),
        size=len(raw),
        sha256=hashlib.sha256(raw).hexdigest().upper(),
    )


def collect_artifacts(mod_dir: Path, manifest: dict[str, Any]) -> list[Artifact]:
    paths: list[Path] = []
    for path in mod_dir.rglob("*"):
        if path.is_dir():
            value = path.stat()
            attributes = getattr(value, "st_file_attributes", 0)
            if path.is_symlink() or attributes & REPARSE_ATTRIBUTE:
                raise PackageError(f"reparse directory is not allowed: {path}")
            continue
        if path.name == LOCK_NAME:
            continue
        paths.append(path)
    if len(paths) > MAX_ARTIFACTS:
        raise PackageError(f"mod contains more than {MAX_ARTIFACTS} artifacts")
    paths.sort(key=lambda item: item.relative_to(mod_dir).as_posix().casefold())
    relative_names = [item.relative_to(mod_dir).as_posix() for item in paths]
    folded = [item.casefold() for item in relative_names]
    if len(folded) != len(set(folded)):
        raise PackageError("case-insensitive artifact path collision")
    return [_read_artifact(mod_dir, path, manifest) for path in paths]


def build_lock(mod_dir: Path) -> dict[str, Any]:
    mod_dir = mod_dir.resolve(strict=True)
    manifest, manifest_raw = validate_manifest(mod_dir)
    catalog_sha256, _ = load_catalog_access()
    artifacts = collect_artifacts(mod_dir, manifest)
    return {
        "$schema": "mod.dluma.lock.schema.json",
        "schema": "dluma.mod.lock.v1",
        "api_version": "1.0",
        "mod_id": manifest["id"],
        "manifest_sha256": hashlib.sha256(manifest_raw).hexdigest().upper(),
        "catalog_sha256": catalog_sha256,
        "artifacts": [artifact.as_json() for artifact in artifacts],
    }


def canonical_lock_bytes(lock: dict[str, Any]) -> bytes:
    return (
        json.dumps(lock, ensure_ascii=False, indent=2, sort_keys=False) + "\n"
    ).encode("utf-8")


def write_lock(mod_dir: Path) -> dict[str, Any]:
    lock = build_lock(mod_dir)
    destination = mod_dir / LOCK_NAME
    temporary = destination.with_name(f".{LOCK_NAME}.{os.getpid()}.tmp")
    try:
        temporary.write_bytes(canonical_lock_bytes(lock))
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    return lock


def validate_lock(mod_dir: Path) -> dict[str, Any]:
    mod_dir = mod_dir.resolve(strict=True)
    actual, _ = load_json_exact(mod_dir / LOCK_NAME, MAX_LOCK_BYTES)
    expected = build_lock(mod_dir)
    compatible_actual = actual
    if actual != expected:
        if (
            actual.get("catalog_sha256")
            == IMMEDIATELY_PREVIOUS_CATALOG_SHA256
        ):
            compatible_actual = dict(actual)
            compatible_actual["catalog_sha256"] = expected["catalog_sha256"]
    if compatible_actual != expected:
        expected_artifacts = {
            item["path"]: item for item in expected["artifacts"]
        }
        actual_artifacts = {
            item.get("path"): item
            for item in actual.get("artifacts", [])
            if isinstance(item, dict)
        }
        changed = sorted(
            path
            for path in set(expected_artifacts) | set(actual_artifacts)
            if expected_artifacts.get(path) != actual_artifacts.get(path)
        )
        detail = f": {', '.join(changed)}" if changed else ""
        raise PackageError(f"lock does not match exact package bytes{detail}")
    return actual


def create_package(mod_dir: Path, destination: Path) -> dict[str, Any]:
    mod_dir = mod_dir.resolve(strict=True)
    lock = validate_lock(mod_dir)
    destination = destination.resolve(strict=False)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination == mod_dir or mod_dir in destination.parents:
        raise PackageError("package output must be outside the mod directory")
    temporary = destination.with_name(f".{destination.name}.{os.getpid()}.tmp")
    try:
        with zipfile.ZipFile(
            temporary,
            "w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
            strict_timestamps=True,
        ) as archive:
            names = [item["path"] for item in lock["artifacts"]] + [LOCK_NAME]
            for relative in sorted(names, key=str.casefold):
                path = mod_dir.joinpath(*PurePosixPath(relative).parts)
                info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                archive.writestr(info, path.read_bytes(), compresslevel=9)
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)
    return {
        "path": str(destination),
        "size": destination.stat().st_size,
        "sha256": hashlib.sha256(destination.read_bytes()).hexdigest().upper(),
        "artifacts": len(lock["artifacts"]) + 1,
    }
