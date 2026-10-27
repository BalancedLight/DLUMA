#!/usr/bin/env python3
"""Validate a literal DLua Lua manifest without executing Lua code."""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path

ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{1,125}[A-Za-z0-9]$")
RESERVED_DEVICE_RE = re.compile(
    r"^(?:con|prn|aux|nul|clock\$|com[1-9]|lpt[1-9])(?:\.|$)", re.IGNORECASE
)
KNOWN_SCOPES = {"global", "project", "map", "local"}
KNOWN_PERMISSIONS = {
    "game.read",
    "player.read",
    "world.query",
    "events.sensor",
    "events.interact",
    "events.enemy_killed",
    "storage.mod",
    "ui.debug",
    "ui.overlay",
    "audio.play",
    "world.write",
    "world.transform",
    "player.write",
    "model.write",
    "model.animate",
    "map.write",
    "camera.effect",
    "ai.spawn",
    "cinematic.control",
    "inventory.write",
    "quest.write",
    "world.global_state",
    "window.control",
    "filesystem.workshop_read",
    "filesystem.arbitrary",
    "process.launch",
    "native.ffi",
}


@dataclass
class LuaMetadataResult:
    path: str
    valid: bool
    id: str = ""
    name: str = ""
    version: str = ""
    scope: str = ""
    enabled_by_default: bool = True
    permissions: list[str] | None = None
    errors: list[str] | None = None
    warnings: list[str] | None = None


def _unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
        return value[1:-1]
    return value


STRING_RE = r'''(?P<quote>["'])(?P<value>(?:\\.|(?!\1).)*)\1'''


def _strip_lua_comments(text: str) -> str:
    """Blank comments while preserving strings and source offsets."""
    output = list(text)
    index = 0
    quote = ""
    while index < len(text):
        char = text[index]
        if quote:
            if char == "\\":
                index += 2
                continue
            if char == quote:
                quote = ""
            index += 1
            continue
        if char in {'"', "'"}:
            quote = char
            index += 1
            continue
        if text.startswith("--[[", index):
            end = text.find("]]", index + 4)
            end = len(text) if end < 0 else end + 2
            for cursor in range(index, end):
                if output[cursor] not in "\r\n":
                    output[cursor] = " "
            index = end
            continue
        if text.startswith("--", index):
            end = text.find("\n", index + 2)
            end = len(text) if end < 0 else end
            for cursor in range(index, end):
                if output[cursor] != "\r":
                    output[cursor] = " "
            index = end
            continue
        index += 1
    return "".join(output)


def _find_manifest_body(text: str) -> tuple[str, int] | None:
    clean = _strip_lua_comments(text)
    locator = list(clean)
    index = 0
    quote = ""
    while index < len(clean):
        if quote:
            if clean[index] == "\\":
                locator[index] = " "
                if index + 1 < len(clean):
                    locator[index + 1] = " "
                index += 2
                continue
            if clean[index] == quote:
                quote = ""
            if locator[index] not in "\r\n":
                locator[index] = " "
        elif clean[index] in {'"', "'"}:
            quote = clean[index]
            locator[index] = " "
        index += 1
    match = re.search(r"\bdlua\s*\.\s*mod\s*\{", "".join(locator))
    if not match:
        return None
    start = match.end()
    depth = 1
    index = start
    quote = ""
    while index < len(clean):
        char = clean[index]
        if quote:
            if char == "\\":
                index += 2
                continue
            if char == quote:
                quote = ""
        elif char in {'"', "'"}:
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return clean[start:index], start
        index += 1
    return clean[start:], start


def _literal_string(body: str, key: str) -> str | None:
    match = re.search(
        rf"\b{re.escape(key)}\s*=\s*([\"'])(?P<value>(?:\\.|(?!\1).)*)\1",
        body,
        re.DOTALL,
    )
    if not match:
        return None
    # Manifest strings are deliberately simple. Lua-style escaped quotes and
    # slashes are accepted without evaluating arbitrary expressions.
    return re.sub(r"\\([\\\"'])", r"\1", match.group("value"))


def _literal_permissions(body: str) -> list[str] | None:
    table = re.search(r"\bpermissions\s*=\s*\{(?P<items>.*?)\}", body, re.DOTALL)
    if table:
        remainder = re.sub(
            r"[\"'](?:\\.|[^\"'\\])*[\"']|[\s,;]", "", table.group("items")
        )
        if remainder:
            return None
        return [
            re.sub(r"\\([\\\"'])", r"\1", match.group("value"))
            for match in re.finditer(STRING_RE, table.group("items"), re.DOTALL)
        ]
    value = _literal_string(body, "permissions")
    if value is not None:
        return [item.strip() for item in value.split(",") if item.strip()]
    if re.search(r"\bpermissions\s*=", body):
        return None
    return []


def _legacy_header(text: str) -> tuple[dict[str, str], bool]:
    fields: dict[str, str] = {}
    saw_header = False
    for raw in text.splitlines()[:96]:
        line = raw.strip()
        if not line:
            continue
        if not line.startswith("--"):
            break
        body = line[2:].lstrip()
        if body.lower().startswith("dlua:"):
            body = body[5:]
        elif body.lower().startswith("dluma:"):
            body = body[4:]
        else:
            continue
        saw_header = True
        if "=" in body:
            key, value = body.split("=", 1)
            fields[key.strip().lower()] = _unquote(value)
    return fields, saw_header


def infer_scope_from_path(path: Path) -> str:
    """Return the lifetime implied by a canonical project-relative path."""
    normalized = "/" + str(path).replace("\\", "/").strip("/").lower() + "/"
    if "/data/scripts/lua/global/" in normalized:
        return "global"
    if "/data/scripts/lua/project/" in normalized:
        return "project"
    if re.search(r"/data/scripts/lua/maps/[^/]+/", normalized):
        return "map"
    if re.search(r"/data/maps/[^/]+/scripts/lua/", normalized):
        return "map"
    # Accepted only as an older colocated prototype path.
    if re.search(r"/data/maps/[^/]+/scripts/dluma/", normalized):
        return "map"
    return ""


def parse_metadata(path: Path) -> LuaMetadataResult:
    result = LuaMetadataResult(
        path=str(path),
        valid=False,
        permissions=[],
        errors=[],
        warnings=[],
    )
    try:
        text = path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as exc:
        result.errors.append(f"cannot read UTF-8 Lua file: {exc}")
        return result

    fields: dict[str, str] = {}
    manifest = _find_manifest_body(text)
    if manifest:
        body, _ = manifest
        for key in ("id", "name", "version", "scope"):
            value = _literal_string(body, key)
            if value is not None:
                fields[key] = value
        enabled = re.search(
            r"\benabled_by_default\s*=\s*(true|false)\b", body, re.IGNORECASE
        )
        if enabled:
            fields["enabled_by_default"] = enabled.group(1)
        permissions = _literal_permissions(body)
        if permissions is None:
            result.errors.append(
                "dlua.mod permissions must be a literal list of quoted permission names"
            )
            permissions = []
        fields["permissions"] = ",".join(permissions)
    else:
        fields, saw_header = _legacy_header(text)
        if not saw_header:
            result.errors.append("missing literal dlua.mod { ... } manifest")
            return result
        result.warnings.append(
            "legacy -- dlua:* metadata is deprecated; move the fields into dlua.mod"
        )

    result.id = fields.get("id", "")
    result.name = fields.get("name", result.id)
    result.version = fields.get("version", "0.0.0")
    declared_scope = fields.get("scope", "").lower()
    inferred_scope = infer_scope_from_path(path)
    if declared_scope == "local":
        result.warnings.append(
            "dlua:scope = local is deprecated; it is treated as project"
        )
        declared_scope = "project"
    result.scope = declared_scope or inferred_scope
    enabled = fields.get("enabled_by_default", "true").strip().lower()
    result.enabled_by_default = enabled not in {"false", "0", "off", "no"}
    result.permissions = sorted(
        {
            item.strip().lower()
            for item in fields.get("permissions", "").split(",")
            if item.strip()
        }
    )

    if not result.id:
        result.errors.append("missing dlua.mod id")
    elif not ID_RE.fullmatch(result.id):
        result.errors.append(
            "dlua.mod id must be 3-127 ASCII letters/digits/dot/underscore/hyphen and cannot end in punctuation"
        )
    elif RESERVED_DEVICE_RE.match(result.id):
        result.errors.append("dlua.mod id cannot use a reserved Windows device name")
    if fields.get("scope", "").lower() not in KNOWN_SCOPES and fields.get("scope"):
        result.errors.append("dlua.mod scope must be global, project, or map")
    elif not result.scope:
        result.errors.append(
            "missing dlua.mod scope outside a canonical global/project/map folder"
        )
    elif inferred_scope and result.scope != inferred_scope:
        result.errors.append(
            f"dlua.mod scope {result.scope!r} conflicts with canonical {inferred_scope!r} folder"
        )
    elif result.scope == "map" and inferred_scope != "map":
        result.errors.append(
            "map scripts must be inside data/scripts/lua/maps/<map>/ or data/maps/<map>/scripts/lua/"
        )
    if not result.name:
        result.errors.append("dlua.mod name is empty")
    if len(result.name) > 128:
        result.errors.append("dlua.mod name is longer than 128 characters")
    if len(result.version) > 64:
        result.errors.append("dlua.mod version is longer than 64 characters")

    unknown = [permission for permission in result.permissions if permission not in KNOWN_PERMISSIONS]
    for permission in unknown:
        result.warnings.append(f"unknown permission: {permission}")

    dangerous = {
        "window.control",
        "filesystem.arbitrary",
        "process.launch",
        "native.ffi",
    }
    requested_dangerous = sorted(dangerous.intersection(result.permissions))
    if requested_dangerous:
        result.warnings.append(
            "dangerous permissions must be explicitly approved in-game: "
            + ", ".join(requested_dangerous)
        )

    result.valid = not result.errors
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    results = [parse_metadata(path) for path in args.paths]
    if args.json:
        print(json.dumps([asdict(result) for result in results], indent=2, ensure_ascii=False))
    else:
        for result in results:
            state = "OK" if result.valid else "ERROR"
            print(f"{state}: {result.path}")
            if result.valid:
                print(
                    f"  id={result.id} scope={result.scope} version={result.version} "
                    f"enabled_by_default={result.enabled_by_default}"
                )
            for error in result.errors or []:
                print(f"  error: {error}")
            for warning in result.warnings or []:
                print(f"  warning: {warning}")
    return 0 if all(result.valid for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
