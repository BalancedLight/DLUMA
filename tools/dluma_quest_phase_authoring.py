"""Secure authoring support for DLUMA stock-carrier quest phases.

This module is used by the Editor integration and is also executable as an
offline validator.  It never edits PXSL source, stock EDF files, or compiled
XML.  A successful custom compile emits only the adjacent loader-owned
``.dlumaq.json`` sidecar.
"""

from __future__ import annotations

import argparse
import contextlib
import ctypes
import hashlib
import json
import math
import os
import re
import stat
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Iterable, Iterator, Mapping, Sequence


SCHEMA = "dluma.quest_phase_bindings.v1"
GENERATED_BY = "dl-universal-loader"
MAX_ARGUMENT_BYTES = 8 * 1024
MAX_JSON_DEPTH = 8
MAX_JSON_NODES = 256
MAX_JSON_STRING_BYTES = 1024
MAX_BINDINGS = 256
MAX_SIDECAR_BYTES = 1024 * 1024
MAX_SOURCE_BYTES = 16 * 1024 * 1024
MAX_OUTPUT_BYTES = 64 * 1024 * 1024

_IDENTIFIER = re.compile(r"^[a-z0-9][a-z0-9._-]{0,126}$")
_ALIAS = re.compile(r"^[a-z0-9][a-z0-9_-]{0,62}$")
_EDF_COMMAND = re.compile(r"^[ \t]*,([A-Za-z0-9_-]+)(?:[ \t]|$)")


class QuestPhaseAuthoringError(ValueError):
    """A deterministic, fail-closed authoring error."""


@dataclass(frozen=True)
class AliasDeclaration:
    command: str
    provider_mod: str
    handler_id: str
    runtime_kind: str
    load_priority: int = 0


@dataclass(frozen=True)
class Directive:
    source_line: int
    runtime_kind: str
    phase_id: str
    provider_mod: str
    handler_id: str
    contract_id: str
    arguments: Mapping[str, Any]
    arguments_json: bytes
    wait: float
    completion: str


@dataclass(frozen=True)
class LockedSnapshot:
    path: Path
    final_path: Path
    data: bytes
    sha256: str
    identity: tuple[int, int, int, int]


def _path_key(path: Path) -> str:
    return os.path.normcase(str(path.resolve(strict=True)))


def _inside(root: Path, child: Path) -> bool:
    try:
        child.resolve(strict=True).relative_to(root.resolve(strict=True))
    except (OSError, ValueError):
        return False
    return True


def _reject_reparse_chain(root: Path, child: Path) -> None:
    root = root.resolve(strict=True)
    # Keep the lexical child path intact while walking it.  Resolving it first
    # would erase a symlink/junction component before lstat can reject it.
    child = Path(os.path.abspath(os.fspath(child)))
    try:
        relative = child.relative_to(root)
    except ValueError as exc:
        raise QuestPhaseAuthoringError(f"path escapes project root: {child}") from exc
    cursor = root
    for part in relative.parts:
        cursor = cursor / part
        info = os.lstat(cursor)
        attributes = getattr(info, "st_file_attributes", 0)
        reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
        if stat.S_ISLNK(info.st_mode) or attributes & reparse_flag:
            raise QuestPhaseAuthoringError(f"reparse path is not permitted: {cursor}")


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise QuestPhaseAuthoringError(f"duplicate JSON object key: {key!r}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> None:
    raise QuestPhaseAuthoringError(f"non-finite JSON number is not permitted: {value}")


def _validate_json_tree(value: Any, *, depth: int = 1) -> int:
    if depth > MAX_JSON_DEPTH:
        raise QuestPhaseAuthoringError("JSON arguments exceed depth 8")
    if isinstance(value, str):
        if len(value.encode("utf-8")) > MAX_JSON_STRING_BYTES:
            raise QuestPhaseAuthoringError("decoded JSON string exceeds 1024 bytes")
        return 1
    if value is None or isinstance(value, (bool, int)):
        return 1
    if isinstance(value, float):
        if not math.isfinite(value):
            raise QuestPhaseAuthoringError("JSON number must be finite")
        return 1
    if isinstance(value, list):
        count = 1
        for item in value:
            count += _validate_json_tree(item, depth=depth + 1)
            if count > MAX_JSON_NODES:
                raise QuestPhaseAuthoringError("JSON arguments exceed 256 nodes")
        return count
    if isinstance(value, dict):
        count = 1
        for key, item in value.items():
            if len(key.encode("utf-8")) > MAX_JSON_STRING_BYTES:
                raise QuestPhaseAuthoringError("decoded JSON key exceeds 1024 bytes")
            count += _validate_json_tree(item, depth=depth + 1)
            if count > MAX_JSON_NODES:
                raise QuestPhaseAuthoringError("JSON arguments exceed 256 nodes")
        return count
    raise QuestPhaseAuthoringError("unsupported JSON value")


def canonicalize_arguments(raw: bytes) -> tuple[dict[str, Any], bytes]:
    if not raw or len(raw) > MAX_ARGUMENT_BYTES:
        raise QuestPhaseAuthoringError("arguments must contain 1..8192 UTF-8 bytes")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise QuestPhaseAuthoringError("arguments are not valid UTF-8") from exc
    try:
        value = json.loads(
            text,
            object_pairs_hook=_strict_object,
            parse_constant=_reject_json_constant,
        )
    except json.JSONDecodeError as exc:
        raise QuestPhaseAuthoringError(f"invalid JSON arguments: {exc.msg}") from exc
    if not isinstance(value, dict):
        raise QuestPhaseAuthoringError("arguments must have a JSON object root")
    _validate_json_tree(value)
    compact = json.dumps(
        value,
        ensure_ascii=False,
        allow_nan=False,
        separators=(",", ":"),
    ).encode("utf-8")
    if len(compact) > MAX_ARGUMENT_BYTES:
        raise QuestPhaseAuthoringError("canonical arguments exceed 8192 bytes")
    return value, compact


def parse_stock_commands(edf_paths: Iterable[Path]) -> set[str]:
    commands: set[str] = set()
    for path in edf_paths:
        try:
            data = path.read_bytes()
            text = data.decode("utf-8-sig", errors="strict")
        except (OSError, UnicodeDecodeError) as exc:
            raise QuestPhaseAuthoringError(f"cannot read stock EDF {path}: {exc}") from exc
        for line in text.splitlines():
            match = _EDF_COMMAND.match(line)
            if match:
                commands.add(match.group(1).casefold())
    return commands


def filter_aliases(
    declarations: Sequence[AliasDeclaration], stock_commands: Iterable[str]
) -> tuple[list[AliasDeclaration], dict[str, str]]:
    occupied = {command.casefold() for command in stock_commands}
    occupied.update({"call-lua", "call-native"})
    alias_counts: dict[str, int] = {}
    for declaration in declarations:
        folded = declaration.command.casefold()
        alias_counts[folded] = alias_counts.get(folded, 0) + 1
    accepted: list[AliasDeclaration] = []
    rejected: dict[str, str] = {}
    for declaration in declarations:
        command = declaration.command.casefold()
        if not _ALIAS.fullmatch(declaration.command):
            rejected[declaration.command] = "invalid alias"
        elif not _IDENTIFIER.fullmatch(declaration.provider_mod):
            rejected[declaration.command] = "invalid provider mod ID"
        elif not _IDENTIFIER.fullmatch(declaration.handler_id):
            rejected[declaration.command] = "invalid handler ID"
        elif declaration.runtime_kind not in {"lua", "native"}:
            rejected[declaration.command] = "invalid runtime kind"
        elif command in occupied or alias_counts[command] != 1:
            rejected[declaration.command] = "case-insensitive command collision"
        else:
            accepted.append(declaration)
    return accepted, rejected


def build_edf(aliases: Sequence[AliasDeclaration]) -> bytes:
    lines = ["# Generated by DL Universal Loader. Do not edit.", ""]
    declarations: list[tuple[str, bool]] = [
        ("call-lua", True),
        ("call-native", True),
        *((alias.command, False) for alias in aliases),
    ]
    for command, builtin in declarations:
        suffix = "name mod handler arguments wait" if builtin else "name arguments wait"
        lines.extend(
            [
                f",{command} {suffix} =",
                "  Phase -type=<<wait guard>> -name=<(,name)>"
                " -time=<(,wait)> -time2p=<(,wait)>"
                " -time3p=<(,wait)> -time4p=<(,wait)>",
                "",
            ]
        )
    return ("\r\n".join(lines) + "\r\n").encode("utf-8")


def _take_token(line: bytes, cursor: int, prefix: bytes) -> tuple[bytes, int]:
    while cursor < len(line) and line[cursor] in b" \t":
        cursor += 1
    if not line.startswith(prefix, cursor):
        raise QuestPhaseAuthoringError(f"expected {prefix.decode('ascii')}")
    cursor += len(prefix)
    end = line.find(b">>", cursor)
    if end < 0:
        raise QuestPhaseAuthoringError("unterminated <<...>> field")
    return line[cursor:end], end + 2


def _validate_text_field(raw: bytes, label: str, maximum: int = 127) -> str:
    if not raw or len(raw) > maximum or b"\x00" in raw:
        raise QuestPhaseAuthoringError(f"{label} is empty or too long")
    try:
        value = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise QuestPhaseAuthoringError(f"{label} is not valid UTF-8") from exc
    if any(ord(char) < 0x20 for char in value):
        raise QuestPhaseAuthoringError(f"{label} contains a control character")
    return value


def _command(line: bytes) -> str | None:
    stripped = line.lstrip(b" \t")
    if not stripped.startswith(b","):
        return None
    end = 1
    while end < len(stripped) and stripped[end] not in b" \t\r\n":
        end += 1
    try:
        return stripped[1:end].decode("ascii", errors="strict").casefold()
    except UnicodeDecodeError:
        return None


def parse_directives(
    source: bytes, aliases: Sequence[AliasDeclaration]
) -> list[Directive]:
    alias_map = {alias.command.casefold(): alias for alias in aliases}
    custom_commands = {"call-lua", "call-native", *alias_map}
    directives: list[Directive] = []
    for line_number, line in enumerate(source.splitlines(), 1):
        command = _command(line)
        if command not in custom_commands:
            continue
        try:
            stripped = line.lstrip(b" \t")
            cursor = 1 + len(command)
            phase_raw, cursor = _take_token(stripped, cursor, b"<<")
            phase_id = _validate_text_field(phase_raw, "phase name")
            alias = alias_map.get(command)
            if alias is None:
                mod_raw, cursor = _take_token(stripped, cursor, b"-mod=<<")
                handler_raw, cursor = _take_token(stripped, cursor, b"-handler=<<")
                provider_mod = _validate_text_field(mod_raw, "provider mod")
                handler_id = _validate_text_field(handler_raw, "handler ID")
                runtime_kind = "lua" if command == "call-lua" else "native"
                if not _IDENTIFIER.fullmatch(provider_mod):
                    raise QuestPhaseAuthoringError("provider mod ID is invalid")
                if not _IDENTIFIER.fullmatch(handler_id):
                    raise QuestPhaseAuthoringError("handler ID is invalid")
            else:
                provider_mod = alias.provider_mod
                handler_id = alias.handler_id
                runtime_kind = alias.runtime_kind
            arguments_raw, cursor = _take_token(stripped, cursor, b"-arguments=<<")
            arguments, arguments_json = canonicalize_arguments(arguments_raw)
            while cursor < len(stripped) and stripped[cursor] in b" \t":
                cursor += 1
            if not stripped.startswith(b"-wait=", cursor):
                raise QuestPhaseAuthoringError("expected -wait=")
            cursor += len(b"-wait=")
            wait_raw = stripped[cursor:].strip()
            if not wait_raw:
                raise QuestPhaseAuthoringError("wait is required")
            try:
                wait_text = wait_raw.decode("ascii", errors="strict")
                wait = float(wait_text)
            except (UnicodeDecodeError, ValueError) as exc:
                raise QuestPhaseAuthoringError("wait must be a decimal number") from exc
            if (
                not math.isfinite(wait)
                or wait < 0
                or wait > 86400
                or not re.fullmatch(r"(?:0|[0-9]+)(?:\.[0-9]+)?", wait_text)
            ):
                raise QuestPhaseAuthoringError("wait must be finite and within 0..86400")
            directives.append(
                Directive(
                    source_line=line_number,
                    runtime_kind=runtime_kind,
                    phase_id=phase_id,
                    provider_mod=provider_mod,
                    handler_id=handler_id,
                    contract_id=f"{provider_mod}:{handler_id}",
                    arguments=arguments,
                    arguments_json=arguments_json,
                    wait=wait,
                    completion=(
                        "fire_and_forget" if wait == 0 else "stock_timed_wait"
                    ),
                )
            )
        except QuestPhaseAuthoringError as exc:
            raise QuestPhaseAuthoringError(
                f"custom quest directive on source line {line_number}: {exc}"
            ) from exc
    if len(directives) > MAX_BINDINGS:
        raise QuestPhaseAuthoringError("quest source exceeds 256 custom phase bindings")
    return directives


def _parse_pxsl_line(value: str | None) -> int | None:
    if value is None or not value.isdecimal():
        return None
    parsed = int(value, 10)
    return parsed if 0 < parsed <= 0xFFFFFFFF else None


def _parse_stock_wait(value: str | None) -> float | None:
    if value is None or not re.fullmatch(r"(?:0|[0-9]+)(?:\.[0-9]+)?", value):
        return None
    parsed = float(value)
    if not math.isfinite(parsed) or parsed < 0 or parsed > 86400:
        return None
    return parsed


def match_directives_to_xml(
    directives: Sequence[Directive], output_xml: bytes
) -> list[tuple[Directive, str]]:
    if len(output_xml) > MAX_OUTPUT_BYTES:
        raise QuestPhaseAuthoringError("compiled quest XML exceeds 64 MiB")
    try:
        root = ET.fromstring(output_xml)
    except ET.ParseError as exc:
        raise QuestPhaseAuthoringError(f"compiled quest XML is malformed: {exc}") from exc
    candidates: list[tuple[str, str, int, float]] = []
    for quest in root.iter("Quest"):
        quest_id = quest.get("name")
        if not quest_id:
            continue
        for phase in quest.iter("Phase"):
            phase_id = phase.get("name")
            pxsl_line = _parse_pxsl_line(phase.get("pxsl_line"))
            waits = [
                _parse_stock_wait(phase.get(name))
                for name in ("time", "time2p", "time3p", "time4p")
            ]
            if (
                phase.get("type") == "wait guard"
                and phase_id
                and pxsl_line
                and waits[0] is not None
                and all(value == waits[0] for value in waits[1:])
            ):
                candidates.append((quest_id, phase_id, pxsl_line, waits[0]))
    matches: list[tuple[Directive, str]] = []
    seen: set[tuple[str, str]] = set()
    for directive in directives:
        found = [
            candidate
            for candidate in candidates
            if candidate[1] == directive.phase_id
            and candidate[2] == directive.source_line
            and candidate[3] == directive.wait
        ]
        if len(found) != 1:
            raise QuestPhaseAuthoringError(
                f"source line {directive.source_line} phase {directive.phase_id!r} "
                f"matched {len(found)} stock wait-guard phases; expected exactly one"
            )
        identity = (found[0][0], found[0][1])
        if identity in seen:
            raise QuestPhaseAuthoringError(
                f"duplicate quest/phase binding: {identity[0]} / {identity[1]}"
            )
        seen.add(identity)
        matches.append((directive, found[0][0]))
    return matches


def _binding_payload(directive: Directive, quest_id: str) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "quest": quest_id,
        "phase": directive.phase_id,
        "source_line": directive.source_line,
        "runtime_kind": directive.runtime_kind,
        "provider_mod": directive.provider_mod,
        "handler": directive.handler_id,
        "contract_id": directive.contract_id,
        "arguments": directive.arguments,
        "wait": directive.wait,
        "completion": directive.completion,
    }
    identity = json.dumps(
        payload, ensure_ascii=False, allow_nan=False, separators=(",", ":")
    ).encode("utf-8")
    payload["binding_hash"] = hashlib.sha256(identity).hexdigest().upper()
    return payload


def build_sidecar(
    *,
    source_path: Path,
    source_sha256: str,
    output_path: Path,
    output_sha256: str,
    matches: Sequence[tuple[Directive, str]],
    provider_hashes: Mapping[str, Mapping[str, str]] | None = None,
) -> bytes:
    bindings = [_binding_payload(directive, quest) for directive, quest in matches]
    document: dict[str, Any] = {
        "schema": SCHEMA,
        "generated_by": GENERATED_BY,
        "source": {"path": str(source_path), "sha256": source_sha256},
        "output": {"path": str(output_path), "sha256": output_sha256},
        "bindings": bindings,
    }
    if provider_hashes:
        diagnostic: dict[str, Mapping[str, str]] = {}
        for provider in sorted({item[0].provider_mod for item in matches}):
            if provider in provider_hashes:
                diagnostic[provider] = provider_hashes[provider]
        if diagnostic:
            document["provider_hashes_diagnostic"] = diagnostic
    encoded = json.dumps(
        document, ensure_ascii=False, allow_nan=False, separators=(",", ":")
    ).encode("utf-8")
    if len(encoded) > MAX_SIDECAR_BYTES:
        raise QuestPhaseAuthoringError("generated sidecar exceeds 1 MiB")
    return encoded


def sidecar_path_for(output_path: Path) -> Path:
    return output_path.with_suffix(".dlumaq.json")


def _read_exact_file(path: Path, maximum: int) -> LockedSnapshot:
    # Python's binary open inherits Windows' deny-write/delete sharing behavior
    # only inconsistently, so the native Editor path uses the Win32 branch.
    if os.name == "nt":
        return _read_exact_file_win32(path, maximum)
    resolved = path.resolve(strict=True)
    before = resolved.stat()
    data = resolved.read_bytes()
    after = resolved.stat()
    identity_before = (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
    identity_after = (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns)
    if identity_before != identity_after or len(data) != before.st_size:
        raise QuestPhaseAuthoringError(f"file identity changed while reading: {path}")
    if len(data) > maximum:
        raise QuestPhaseAuthoringError(f"file exceeds secure size limit: {path}")
    return LockedSnapshot(
        path=path,
        final_path=resolved,
        data=data,
        sha256=hashlib.sha256(data).hexdigest().upper(),
        identity=identity_after,
    )


def _read_exact_file_win32(path: Path, maximum: int) -> LockedSnapshot:
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    create_file = kernel32.CreateFileW
    create_file.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    ]
    create_file.restype = wintypes.HANDLE
    read_file = kernel32.ReadFile
    read_file.argtypes = [
        wintypes.HANDLE,
        wintypes.LPVOID,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
        wintypes.LPVOID,
    ]
    read_file.restype = wintypes.BOOL
    get_size = kernel32.GetFileSizeEx
    get_size.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_longlong)]
    get_size.restype = wintypes.BOOL
    final_name = kernel32.GetFinalPathNameByHandleW
    final_name.argtypes = [wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD]
    final_name.restype = wintypes.DWORD
    close_handle = kernel32.CloseHandle
    invalid = wintypes.HANDLE(-1).value
    handle = create_file(
        str(path),
        0x80000000,  # GENERIC_READ
        0x00000001,  # FILE_SHARE_READ: deny write and delete
        None,
        3,  # OPEN_EXISTING
        0x02000000,  # FILE_FLAG_BACKUP_SEMANTICS
        None,
    )
    if handle == invalid:
        raise QuestPhaseAuthoringError(
            f"cannot securely open {path}: Win32 error {ctypes.get_last_error()}"
        )
    try:
        size = ctypes.c_longlong()
        if not get_size(handle, ctypes.byref(size)) or size.value < 0 or size.value > maximum:
            raise QuestPhaseAuthoringError(f"file exceeds secure size limit: {path}")
        buffer = bytearray(size.value)
        offset = 0
        while offset < size.value:
            take = min(1024 * 1024, size.value - offset)
            chunk = (ctypes.c_ubyte * take).from_buffer(buffer, offset)
            read = wintypes.DWORD()
            if not read_file(handle, chunk, take, ctypes.byref(read), None) or read.value != take:
                raise QuestPhaseAuthoringError(f"short or failed read: {path}")
            offset += take
        path_buffer = ctypes.create_unicode_buffer(32768)
        length = final_name(handle, path_buffer, len(path_buffer), 0)
        if not length or length >= len(path_buffer):
            raise QuestPhaseAuthoringError(f"cannot resolve handle-derived path: {path}")
        final_text = path_buffer.value
        if final_text.startswith("\\\\?\\UNC\\"):
            final_text = "\\\\" + final_text[8:]
        elif final_text.startswith("\\\\?\\"):
            final_text = final_text[4:]
        final_path = Path(final_text).resolve(strict=True)
        info = os.stat(final_path)
        identity = (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)
        data = bytes(buffer)
        if info.st_size != len(data):
            raise QuestPhaseAuthoringError(f"file identity changed while reading: {path}")
        return LockedSnapshot(
            path=path,
            final_path=final_path,
            data=data,
            sha256=hashlib.sha256(data).hexdigest().upper(),
            identity=identity,
        )
    finally:
        close_handle(handle)


def _open_win32_source_guard(path: Path) -> tuple[Any, Callable[[Any], None]]:
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    create_file = kernel32.CreateFileW
    create_file.argtypes = [
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    ]
    create_file.restype = wintypes.HANDLE
    close_handle = kernel32.CloseHandle
    close_handle.argtypes = [wintypes.HANDLE]
    close_handle.restype = wintypes.BOOL
    invalid = wintypes.HANDLE(-1).value
    handle = create_file(
        str(path),
        0x80000000,
        0x00000001,  # FILE_SHARE_READ only: deny mutation and replacement.
        None,
        3,
        0x02000000,
        None,
    )
    if handle == invalid:
        raise QuestPhaseAuthoringError(
            f"cannot lock quest source {path}: Win32 error {ctypes.get_last_error()}"
        )
    return handle, close_handle


@contextlib.contextmanager
def locked_source_snapshot(
    source_path: Path, project_root: Path
) -> Iterator[LockedSnapshot]:
    root = project_root.resolve(strict=True)
    _reject_reparse_chain(root, source_path)
    guard: Any = None
    closer: Callable[[Any], None] | None = None
    if os.name == "nt":
        guard, closer = _open_win32_source_guard(source_path)
    try:
        snapshot = _read_exact_file(source_path, MAX_SOURCE_BYTES)
        if not _inside(root, snapshot.final_path):
            raise QuestPhaseAuthoringError("handle-derived source path escapes project root")
        yield snapshot
        current = _read_exact_file(source_path, MAX_SOURCE_BYTES)
        if current.identity != snapshot.identity or current.sha256 != snapshot.sha256:
            raise QuestPhaseAuthoringError("quest source changed during compilation")
    finally:
        if guard is not None and closer is not None:
            closer(guard)


def _is_loader_sidecar(path: Path) -> bool:
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_strict_object)
    except (OSError, UnicodeError, json.JSONDecodeError, QuestPhaseAuthoringError):
        return False
    return (
        isinstance(value, dict)
        and value.get("schema") == SCHEMA
        and value.get("generated_by") == GENERATED_BY
    )


def _atomic_publish(path: Path, data: bytes, project_root: Path) -> None:
    if len(data) > MAX_SIDECAR_BYTES:
        raise QuestPhaseAuthoringError("sidecar exceeds 1 MiB")
    root = project_root.resolve(strict=True)
    parent = path.parent.resolve(strict=True)
    if not _inside(root, parent):
        raise QuestPhaseAuthoringError("sidecar destination escapes project root")
    _reject_reparse_chain(root, parent)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb", closefd=True) as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except OSError as exc:
        raise QuestPhaseAuthoringError(f"cannot atomically publish sidecar: {exc}") from exc
    finally:
        with contextlib.suppress(FileNotFoundError):
            temporary.unlink()


def validate_sidecar(
    *, sidecar_path: Path, output_path: Path, project_root: Path
) -> dict[str, Any]:
    """Validate immutable sidecar bytes against authoritative project files."""
    root = project_root.resolve(strict=True)
    _reject_reparse_chain(root, sidecar_path)
    _reject_reparse_chain(root, output_path)
    sidecar = _read_exact_file(sidecar_path, MAX_SIDECAR_BYTES)
    output = _read_exact_file(output_path, MAX_OUTPUT_BYTES)
    if not _inside(root, sidecar.final_path) or not _inside(root, output.final_path):
        raise QuestPhaseAuthoringError("sidecar or compiled XML escapes project root")
    expected_sidecar = sidecar_path_for(output.final_path)
    if _path_key(sidecar.final_path) != _path_key(expected_sidecar):
        raise QuestPhaseAuthoringError("sidecar is not adjacent to its authoritative XML")
    try:
        document = json.loads(
            sidecar.data.decode("utf-8", errors="strict"),
            object_pairs_hook=_strict_object,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise QuestPhaseAuthoringError(f"sidecar JSON is malformed: {exc}") from exc
    if not isinstance(document, dict):
        raise QuestPhaseAuthoringError("sidecar root must be an object")
    if document.get("schema") != SCHEMA or document.get("generated_by") != GENERATED_BY:
        raise QuestPhaseAuthoringError("sidecar schema/ownership marker is invalid")
    source_record = document.get("source")
    output_record = document.get("output")
    bindings = document.get("bindings")
    if not isinstance(source_record, dict) or not isinstance(output_record, dict):
        raise QuestPhaseAuthoringError("sidecar source/output identity is missing")
    if not isinstance(bindings, list) or not bindings or len(bindings) > MAX_BINDINGS:
        raise QuestPhaseAuthoringError("sidecar must contain 1..256 bindings")
    if output_record.get("path") != str(output.final_path):
        raise QuestPhaseAuthoringError("sidecar output path identity changed")
    if output_record.get("sha256") != output.sha256:
        raise QuestPhaseAuthoringError("compiled XML hash does not match sidecar")
    source_value = source_record.get("path")
    if not isinstance(source_value, str):
        raise QuestPhaseAuthoringError("sidecar source path is invalid")
    source_path = Path(source_value)
    _reject_reparse_chain(root, source_path)
    source = _read_exact_file(source_path, MAX_SOURCE_BYTES)
    if not _inside(root, source.final_path) or source_record.get("path") != str(source.final_path):
        raise QuestPhaseAuthoringError("sidecar source path identity changed")
    if source_record.get("sha256") != source.sha256:
        raise QuestPhaseAuthoringError("quest source hash does not match sidecar")
    seen: set[tuple[str, str]] = set()
    required = {
        "quest",
        "phase",
        "source_line",
        "runtime_kind",
        "provider_mod",
        "handler",
        "contract_id",
        "arguments",
        "wait",
        "completion",
        "binding_hash",
    }
    for index, binding in enumerate(bindings):
        if not isinstance(binding, dict) or set(binding) != required:
            raise QuestPhaseAuthoringError(f"bindings[{index}] has an invalid field set")
        quest = binding["quest"]
        phase = binding["phase"]
        line = binding["source_line"]
        runtime_kind = binding["runtime_kind"]
        provider = binding["provider_mod"]
        handler = binding["handler"]
        wait = binding["wait"]
        if (
            not isinstance(quest, str)
            or not quest
            or len(quest.encode("utf-8")) > 127
            or not isinstance(phase, str)
            or not phase
            or len(phase.encode("utf-8")) > 127
            or not isinstance(line, int)
            or isinstance(line, bool)
            or line <= 0
            or line > 0xFFFFFFFF
            or runtime_kind not in {"lua", "native"}
            or not isinstance(provider, str)
            or not _IDENTIFIER.fullmatch(provider)
            or not isinstance(handler, str)
            or not _IDENTIFIER.fullmatch(handler)
            or binding["contract_id"] != f"{provider}:{handler}"
            or not isinstance(wait, (int, float))
            or isinstance(wait, bool)
            or not math.isfinite(wait)
            or wait < 0
            or wait > 86400
            or binding["completion"]
            != ("fire_and_forget" if wait == 0 else "stock_timed_wait")
        ):
            raise QuestPhaseAuthoringError(f"bindings[{index}] contains invalid values")
        arguments = binding["arguments"]
        if not isinstance(arguments, dict):
            raise QuestPhaseAuthoringError(f"bindings[{index}].arguments must be an object")
        raw_arguments = json.dumps(
            arguments, ensure_ascii=False, allow_nan=False, separators=(",", ":")
        ).encode("utf-8")
        canonicalize_arguments(raw_arguments)
        rebuilt = dict(binding)
        claimed_hash = rebuilt.pop("binding_hash")
        expected_hash = hashlib.sha256(
            json.dumps(
                rebuilt, ensure_ascii=False, allow_nan=False, separators=(",", ":")
            ).encode("utf-8")
        ).hexdigest().upper()
        if claimed_hash != expected_hash:
            raise QuestPhaseAuthoringError(f"bindings[{index}] hash is invalid")
        identity = (quest, phase)
        if identity in seen:
            raise QuestPhaseAuthoringError(f"duplicate quest/phase binding: {quest} / {phase}")
        seen.add(identity)
    return document


def compile_and_emit(
    *,
    source_path: Path,
    output_path: Path,
    project_root: Path,
    aliases: Sequence[AliasDeclaration],
    compiler: Callable[[Path, Path], bool],
    provider_hashes: Mapping[str, Mapping[str, str]] | None = None,
) -> Path | None:
    root = project_root.resolve(strict=True)
    if not _inside(root, source_path) or not _inside(root, output_path.parent):
        raise QuestPhaseAuthoringError("source/output path escapes project root")
    with locked_source_snapshot(source_path, root) as source:
        directives = parse_directives(source.data, aliases)
        if not compiler(source.final_path, output_path):
            raise QuestPhaseAuthoringError("stock quest compiler reported failure")
    _reject_reparse_chain(root, output_path)
    output = _read_exact_file(output_path, MAX_OUTPUT_BYTES)
    if not _inside(root, output.final_path):
        raise QuestPhaseAuthoringError("handle-derived output path escapes project root")
    sidecar_path = sidecar_path_for(output.final_path)
    if not directives:
        if sidecar_path.exists() and _is_loader_sidecar(sidecar_path):
            sidecar_path.unlink()
        return None
    matches = match_directives_to_xml(directives, output.data)
    sidecar = build_sidecar(
        source_path=source.final_path,
        source_sha256=source.sha256,
        output_path=output.final_path,
        output_sha256=output.sha256,
        matches=matches,
        provider_hashes=provider_hashes,
    )
    _atomic_publish(sidecar_path, sidecar, root)
    published = _read_exact_file(sidecar_path, MAX_SIDECAR_BYTES)
    if published.data != sidecar:
        raise QuestPhaseAuthoringError("published sidecar bytes failed revalidation")
    return sidecar_path


def _load_aliases(path: Path | None) -> list[AliasDeclaration]:
    if path is None:
        return []
    try:
        document = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_strict_object)
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise QuestPhaseAuthoringError(f"cannot load alias inventory: {exc}") from exc
    if not isinstance(document, list):
        raise QuestPhaseAuthoringError("alias inventory must be a JSON array")
    aliases: list[AliasDeclaration] = []
    for index, value in enumerate(document):
        if not isinstance(value, dict):
            raise QuestPhaseAuthoringError(f"aliases[{index}] must be an object")
        try:
            aliases.append(
                AliasDeclaration(
                    command=value["command"],
                    provider_mod=value["provider_mod"],
                    handler_id=value["handler_id"],
                    runtime_kind=value["runtime_kind"],
                    load_priority=int(value.get("load_priority", 0)),
                )
            )
        except (KeyError, TypeError, ValueError) as exc:
            raise QuestPhaseAuthoringError(f"aliases[{index}] is malformed") from exc
    return aliases


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--project-root", type=Path, required=True)
    parser.add_argument("--stock-edf", type=Path, action="append", default=[])
    parser.add_argument("--aliases", type=Path)
    parser.add_argument("--write-edf", type=Path)
    parser.add_argument(
        "--validate-existing-output",
        action="store_true",
        help="emit/validate a sidecar without invoking the stock compiler",
    )
    args = parser.parse_args(argv)
    try:
        declarations = _load_aliases(args.aliases)
        stock = parse_stock_commands(args.stock_edf)
        aliases, rejected = filter_aliases(declarations, stock)
        if args.write_edf:
            _atomic_publish(args.write_edf, build_edf(aliases), args.project_root)
        if not args.validate_existing_output:
            raise QuestPhaseAuthoringError(
                "offline CLI requires --validate-existing-output; the native Editor owns compilation"
            )
        result = compile_and_emit(
            source_path=args.source,
            output_path=args.output,
            project_root=args.project_root,
            aliases=aliases,
            compiler=lambda _source, output: output.is_file(),
        )
    except QuestPhaseAuthoringError as exc:
        print(f"quest phase authoring failed: {exc}", file=sys.stderr)
        return 2
    for command, reason in sorted(rejected.items()):
        print(f"alias rejected: {command}: {reason}", file=sys.stderr)
    print(f"quest phase sidecar: {result if result else 'not required'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
