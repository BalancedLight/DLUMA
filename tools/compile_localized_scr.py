#!/usr/bin/env python3
"""Parse loader localization SCR and emit a deterministic UTF-16LE BIN.

The game consumes the source SCR from the loader PAK.  This small compiler is
intentionally repository-owned: it is used by validation and tests to prove
that comments, includes, escapes, and non-Latin values survive a compiled
round-trip without relying on the game runtime.
"""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path
from typing import Iterable

MAGIC = b"DLWLOC1\0"
_IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class ScrError(ValueError):
    pass


def _strip_comments(text: str) -> str:
    out: list[str] = []
    i = 0
    quote = False
    escaped = False
    while i < len(text):
        if quote:
            out.append(text[i])
            if escaped:
                escaped = False
            elif text[i] == "\\":
                escaped = True
            elif text[i] == '"':
                quote = False
            i += 1
            continue
        if text[i] == '"':
            quote = True
            out.append(text[i])
            i += 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = len(text) if end < 0 else end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise ScrError("unterminated block comment")
            i = end + 2
        else:
            out.append(text[i])
            i += 1
    if quote:
        raise ScrError("unterminated string literal")
    return "".join(out)


def _decode_string(source: str, start: int) -> tuple[str, int]:
    if start >= len(source) or source[start] != '"':
        raise ScrError("expected quoted string")
    i = start + 1
    chars: list[str] = []
    escapes = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}
    while i < len(source):
        ch = source[i]
        if ch == '"':
            return "".join(chars), i + 1
        if ch != "\\":
            chars.append(ch)
            i += 1
            continue
        i += 1
        if i >= len(source):
            raise ScrError("unterminated escape")
        esc = source[i]
        if esc not in escapes:
            raise ScrError(f"invalid escape \\{esc}")
        chars.append(escapes[esc])
        i += 1
    raise ScrError("unterminated string literal")


def _skip_ws(source: str, index: int) -> int:
    while index < len(source) and source[index].isspace():
        index += 1
    return index


def parse_scr(path: Path, _include_stack: tuple[Path, ...] = ()) -> dict[str, str]:
    path = path.resolve()
    if path in _include_stack:
        raise ScrError(f"recursive include: {path}")
    try:
        text = path.read_text(encoding="utf-8-sig")
    except UnicodeDecodeError as exc:
        raise ScrError(f"{path}: not valid UTF-8") from exc
    source = _strip_comments(text)
    values: dict[str, str] = {}
    i = 0
    while True:
        i = _skip_ws(source, i)
        if i >= len(source):
            break
        if source.startswith("!String(s, s)", i):
            i += len("!String(s, s)")
            continue
        if source.startswith("!include", i):
            i += len("!include")
            i = _skip_ws(source, i)
            include, i = _decode_string(source, i)
            include_path = (path.parent / include).resolve()
            for key, value in parse_scr(include_path, _include_stack + (path,)).items():
                if key in values:
                    raise ScrError(f"{path}: duplicate key {key}")
                values[key] = value
            continue
        match = _IDENT.match(source, i)
        if not match:
            raise ScrError(f"{path}: unexpected syntax near {source[i:i + 24]!r}")
        function = match.group(0)
        i = _skip_ws(source, match.end())
        if function == "PlatformSpecific":
            if i >= len(source) or source[i] != "(":
                raise ScrError(f"{path}: PlatformSpecific requires an opening parenthesis")
            i = _skip_ws(source, i + 1)
            _, i = _decode_string(source, i)
            i = _skip_ws(source, i)
            if i >= len(source) or source[i] != ")":
                raise ScrError(f"{path}: PlatformSpecific requires a closing parenthesis")
            i = _skip_ws(source, i + 1)
            if i >= len(source) or source[i] != "{":
                raise ScrError(f"{path}: PlatformSpecific requires a block")
            depth = 1
            block_start = i + 1
            i += 1
            while i < len(source) and depth:
                if source[i] == "{":
                    depth += 1
                elif source[i] == "}":
                    depth -= 1
                i += 1
            if depth:
                raise ScrError(f"{path}: unterminated PlatformSpecific block")
            nested = parse_scr_text(source[block_start:i - 1], path)
            for key, value in nested.items():
                if key in values:
                    raise ScrError(f"{path}: duplicate key {key}")
                values[key] = value
            continue
        if function != "String":
            raise ScrError(f"{path}: unsupported directive {function}")
        i = _skip_ws(source, i)
        if i >= len(source) or source[i] != "(":
            raise ScrError(f"{path}: String requires an opening parenthesis")
        i = _skip_ws(source, i + 1)
        key, i = _decode_string(source, i)
        i = _skip_ws(source, i)
        if i >= len(source) or source[i] != ",":
            raise ScrError(f"{path}: String requires key,value")
        i = _skip_ws(source, i + 1)
        value, i = _decode_string(source, i)
        i = _skip_ws(source, i)
        if i >= len(source) or source[i] != ")":
            raise ScrError(f"{path}: String requires a closing parenthesis")
        i += 1
        if not key or not key.isascii() or not key.startswith("DLUMA_"):
            raise ScrError(f"{path}: invalid localization key {key!r}")
        if key in values:
            raise ScrError(f"{path}: duplicate key {key}")
        values[key] = value
    return values


def parse_scr_text(source: str, path: Path) -> dict[str, str]:
    temporary = path.with_name(path.name + ".nested")
    # Nested PlatformSpecific blocks do not contain includes in our catalogs;
    # parse the same grammar without touching disk by wrapping the source in a
    # tiny scanner through a private temporary in-memory equivalent.
    values: dict[str, str] = {}
    i = 0
    while i < len(source):
        i = _skip_ws(source, i)
        if i >= len(source):
            break
        match = _IDENT.match(source, i)
        if not match or match.group(0) != "String":
            raise ScrError(f"{path}: invalid nested PlatformSpecific syntax")
        i = _skip_ws(source, match.end())
        if i >= len(source) or source[i] != "(":
            raise ScrError(f"{path}: malformed nested String")
        i = _skip_ws(source, i + 1)
        key, i = _decode_string(source, i)
        i = _skip_ws(source, i)
        if i >= len(source) or source[i] != ",":
            raise ScrError(f"{path}: malformed nested String")
        value, i = _decode_string(source, _skip_ws(source, i + 1))
        i = _skip_ws(source, i)
        if i >= len(source) or source[i] != ")":
            raise ScrError(f"{path}: malformed nested String")
        i += 1
        if not key or not key.isascii() or not key.startswith("DLUMA_") or key in values:
            raise ScrError(f"{path}: invalid or duplicate nested key {key!r}")
        values[key] = value
    return values


def compile_values(values: dict[str, str]) -> bytes:
    payload = bytearray(MAGIC)
    payload += struct.pack("<I", len(values))
    for key in sorted(values):
        key_bytes = key.encode("ascii")
        value_bytes = values[key].encode("utf-16le")
        if len(key_bytes) > 0xFFFF:
            raise ScrError(f"key too long: {key}")
        payload += struct.pack("<H", len(key_bytes))
        payload += key_bytes
        payload += struct.pack("<I", len(value_bytes) // 2)
        payload += value_bytes
    return bytes(payload)


def compile_file(source: Path, output: Path) -> dict[str, str]:
    values = parse_scr(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(compile_values(values))
    return values


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    compile_file(args.source, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
