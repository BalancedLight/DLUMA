#!/usr/bin/env python3
"""Offline syntax validator for DL Universal Loader .dlscript files."""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable

EVENTS = {"map_load", "map_unload"}
FUTURE_EVENTS = {"trigger_enter", "trigger_exit", "interact"}
KEY_RE = re.compile(r"^(?:F(?:[1-9]|1[0-2])|[A-Z0-9]|(?:0x[0-9a-fA-F]+|\d+))$")
QUOTED = r'"(?:\\.|[^"\\])*"'


@dataclass
class ValidationResult:
    path: str
    valid: bool
    script_id: str = ""
    name: str = ""
    event_count: int = 0
    action_count: int = 0
    reserved_event_count: int = 0
    errors: list[str] | None = None
    warnings: list[str] | None = None


def strip_comment(line: str) -> str:
    quoted = False
    escaped = False
    for i, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if quoted and char == "\\":
            escaped = True
            continue
        if char == '"':
            quoted = not quoted
            continue
        if not quoted and char == "#":
            return line[:i]
        if not quoted and line.startswith("//", i):
            return line[:i]
    return line


def quoted_values(text: str) -> list[str]:
    values: list[str] = []
    for match in re.finditer(QUOTED, text):
        token = match.group(0)[1:-1]
        values.append(bytes(token, "utf-8").decode("unicode_escape"))
    return values


def validate_text(text: str, path: str = "<memory>") -> ValidationResult:
    errors: list[str] = []
    warnings: list[str] = []
    script_id = ""
    name = ""
    current_event: str | None = None
    event_count = 0
    action_count = 0
    reserved_count = 0
    saw_header = False

    lines = text.removeprefix("\ufeff").splitlines()
    for number, raw in enumerate(lines, 1):
        line = strip_comment(raw).strip()
        if not line:
            continue
        if not saw_header:
            if line != "dlscript 1":
                errors.append(f"{number}: first statement must be 'dlscript 1'")
                break
            saw_header = True
            continue

        if current_event is None:
            if line.startswith("id "):
                values = quoted_values(line[3:])
                if len(values) != 1 or not values[0]:
                    errors.append(f"{number}: id requires one nonempty quoted value")
                else:
                    script_id = values[0]
                continue
            if line.startswith("name "):
                values = quoted_values(line[5:])
                if len(values) != 1:
                    errors.append(f"{number}: name requires one quoted value")
                else:
                    name = values[0]
                continue
            if line.startswith("every "):
                try:
                    interval = int(line[6:].strip(), 0)
                except ValueError:
                    interval = 0
                if not 50 <= interval <= 86_400_000:
                    errors.append(f"{number}: every interval must be 50..86400000 ms")
                else:
                    current_event = "timer"
                    event_count += 1
                continue
            if line.startswith("on "):
                spec = line[3:].strip()
                first, _, tail = spec.partition(" ")
                if first in EVENTS:
                    if tail.strip():
                        errors.append(f"{number}: {first} takes no argument")
                    current_event = first
                    event_count += 1
                elif first == "key":
                    key = tail.strip().upper()
                    if not KEY_RE.fullmatch(key):
                        errors.append(f"{number}: invalid key '{tail.strip()}'")
                    else:
                        current_event = first
                        event_count += 1
                elif first in FUTURE_EVENTS:
                    values = quoted_values(tail)
                    if len(values) != 1 or not values[0]:
                        errors.append(f"{number}: {first} requires one quoted editor object name")
                    else:
                        current_event = first
                        event_count += 1
                        reserved_count += 1
                else:
                    errors.append(f"{number}: unknown event '{first}'")
                continue
            errors.append(f"{number}: statement is not valid outside an event block")
            continue

        if line == "end":
            current_event = None
            continue
        if line.startswith("log "):
            if len(quoted_values(line[4:])) != 1:
                errors.append(f"{number}: log requires one quoted message")
            else:
                action_count += 1
            continue
        if line.startswith("marker "):
            if len(quoted_values(line[7:])) != 2:
                errors.append(f"{number}: marker requires quoted filename and contents")
            else:
                action_count += 1
            continue
        if line == "dump_context":
            action_count += 1
            continue
        errors.append(f"{number}: unknown action")

    if saw_header and current_event is not None:
        errors.append("EOF: event block is missing end")
    if not saw_header:
        errors.append("missing dlscript header")
    if event_count == 0:
        errors.append("script has no events")
    if reserved_count:
        warnings.append(
            f"{reserved_count} trigger/interact event(s) are reserved syntax and are not dispatched by prototype 0.1.2"
        )
    return ValidationResult(
        path=path,
        valid=not errors,
        script_id=script_id,
        name=name,
        event_count=event_count,
        action_count=action_count,
        reserved_event_count=reserved_count,
        errors=errors,
        warnings=warnings,
    )


def validate_file(path: Path) -> ValidationResult:
    try:
        text = path.read_text(encoding="utf-8-sig")
    except (OSError, UnicodeError) as exc:
        return ValidationResult(str(path), False, errors=[str(exc)], warnings=[])
    return validate_text(text, str(path))


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("scripts", nargs="+", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    results = [validate_file(path) for path in args.scripts]
    if args.json:
        print(json.dumps([asdict(result) for result in results], indent=2))
    else:
        for result in results:
            print(f"{'OK' if result.valid else 'ERROR'}: {result.path}")
            print(f"  id={result.script_id or '<default filename>'} events={result.event_count} actions={result.action_count}")
            for warning in result.warnings or []:
                print(f"  warning: {warning}")
            for error in result.errors or []:
                print(f"  error: {error}")
    return 0 if all(result.valid for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
