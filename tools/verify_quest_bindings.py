"""Validate native quest transition routes against compiled quest XML.

The game owns native quest parsing and synchronization.  This tool deliberately
does not compile, rewrite, or mount quest content: it verifies the narrow
manifest used by custom-map scripts before a Workshop item is packaged.

Example manifest::

    {
      "bindings": [{
        "project": "Mesmerized",
        "map": "nowhere",
        "quest": "DLUMA_Rescue",
        "phases": {"reach_lab": "DLUMA_ReachLab"}
      }]
    }

Each native quest and phase selector must resolve exactly once across the XML
files passed on the command line.  Ambiguous selectors are rejected instead of
guessing which native phase a scripted transition should control.  A binding also
declares its exact custom project and map.  Supplying ``--project-root`` makes
the tool reject compiled output outside that project and verifies that every
binding names the selected project/map route.
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


class BindingError(ValueError):
    """A user-correctable authored binding error."""


def _as_nonempty_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BindingError(f"{label} must be a non-empty string")
    return value


def _as_identifier(value: Any, label: str, *, maximum: int = 96) -> str:
    value = _as_nonempty_string(value, label)
    if len(value) > maximum or any(
        not ("A" <= char <= "Z" or "a" <= char <= "z" or "0" <= char <= "9" or char in "_.-")
        for char in value
    ):
        raise BindingError(f"{label} must be an exact bounded identifier")
    return value


def _inside(root: Path, child: Path) -> bool:
    try:
        child.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def _path_key(path: Path) -> str:
    """Use the host's case-insensitive project-path identity consistently."""
    return str(path.resolve()).casefold()


def discover_project_quest_xml(project_root: Path) -> list[Path]:
    """Return every parseable XML file under a project that declares a Quest.

    Compiler output names are not a stable engine contract across tool versions,
    so this deliberately discovers semantic ``<Quest>`` content rather than
    trusting a caller-supplied filename list. Malformed files whose names look
    quest-related are hard failures; unrelated malformed XML is ignored.
    """
    root = project_root.resolve()
    if not root.is_dir():
        raise BindingError(f"custom project root is not a directory: {project_root}")
    discovered: list[Path] = []
    for path in sorted(root.rglob("*.xml")):
        if not path.is_file():
            continue
        try:
            document = ET.parse(path).getroot()
        except (ET.ParseError, OSError) as exc:
            if "quest" in path.stem.casefold():
                raise BindingError(
                    f"cannot read compiled quest XML {path}: {exc}"
                ) from exc
            continue
        if any(True for _ in document.iter("Quest")):
            discovered.append(path.resolve())
    return discovered


def _read_quests(
    paths: list[Path],
) -> dict[str, list[tuple[Path, Counter[str]]]]:
    quests: dict[str, list[tuple[Path, Counter[str]]]] = defaultdict(list)
    for path in paths:
        try:
            root = ET.parse(path).getroot()
        except (ET.ParseError, OSError) as exc:
            raise BindingError(f"cannot read compiled quest XML {path}: {exc}") from exc
        for quest in root.iter("Quest"):
            name = quest.get("name")
            if not name:
                continue
            phase_names: Counter[str] = Counter(
                phase.get("name")
                for phase in quest.iter("Phase")
                if phase.get("name")
            )
            quests[name].append((path, phase_names))
    return quests


def validate(
    bindings_path: Path,
    xml_paths: list[Path],
    *,
    project_root: Path | None = None,
    project: str | None = None,
    map_name: str | None = None,
) -> None:
    try:
        document = json.loads(bindings_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise BindingError(f"cannot read bindings manifest {bindings_path}: {exc}") from exc
    bindings = document.get("bindings") if isinstance(document, dict) else None
    if not isinstance(bindings, list) or not bindings:
        raise BindingError("bindings manifest must contain a non-empty 'bindings' array")
    if not xml_paths:
        raise BindingError("at least one compiled quest XML file is required")
    resolved_xml_paths: list[Path] = []
    seen_xml_paths: set[str] = set()
    for xml_path in xml_paths:
        resolved = xml_path.resolve()
        key = _path_key(resolved)
        if key not in seen_xml_paths:
            seen_xml_paths.add(key)
            resolved_xml_paths.append(resolved)
    selected_project_root: Path | None = None
    if project_root is not None:
        selected_project_root = project_root.resolve()
        if not selected_project_root.is_dir():
            raise BindingError(f"custom project root is not a directory: {project_root}")
        for xml_path in resolved_xml_paths:
            if not _inside(selected_project_root, xml_path):
                raise BindingError(
                    f"compiled quest XML is outside custom project root: {xml_path}"
                )
        supplied = {_path_key(path) for path in resolved_xml_paths}
        omitted = [
            path for path in discover_project_quest_xml(selected_project_root)
            if _path_key(path) not in supplied
        ]
        if omitted:
            names = ", ".join(str(path) for path in omitted)
            raise BindingError(
                "compiled quest XML was not supplied for the selected project: " + names
            )
        project = _as_identifier(project or selected_project_root.name, "selected project")
    elif project is not None:
        project = _as_identifier(project, "selected project")
    if map_name is not None:
        map_name = _as_identifier(map_name, "selected map")
    quests = _read_quests(resolved_xml_paths)
    seen_binding_ids: Counter[str] = Counter()
    seen_native_quests: Counter[str] = Counter()
    for index, binding in enumerate(bindings):
        prefix = f"bindings[{index}]"
        if not isinstance(binding, dict):
            raise BindingError(f"{prefix} must be an object")
        binding_id = _as_identifier(binding.get("id"), f"{prefix}.id", maximum=48)
        binding_project = _as_identifier(binding.get("project"), f"{prefix}.project")
        binding_map = _as_identifier(binding.get("map"), f"{prefix}.map")
        native_quest = _as_identifier(binding.get("quest"), f"{prefix}.quest")
        if project is not None and binding_project != project:
            raise BindingError(
                f"{prefix}: binding project '{binding_project}' does not match selected project '{project}'"
            )
        if map_name is not None and binding_map != map_name:
            raise BindingError(
                f"{prefix}: binding map '{binding_map}' does not match selected map '{map_name}'"
            )
        # Binding names are case-insensitive for manifest ergonomics, while
        # authored game quest/phase selectors remain exact-case identifiers.
        seen_binding_ids[binding_id.lower()] += 1
        seen_native_quests[native_quest] += 1
        matches = quests.get(native_quest, [])
        if not matches:
            raise BindingError(f"{prefix}: native quest '{native_quest}' was not found")
        if len(matches) != 1:
            files = ", ".join(str(path) for path, _ in matches)
            raise BindingError(
                f"{prefix}: native quest '{native_quest}' is ambiguous across {files}"
            )
        phases = binding.get("phases", {})
        if not isinstance(phases, dict) or not phases:
            raise BindingError(
                f"{prefix}.phases must be a non-empty object of route-to-native phase mappings"
            )
        available_phases = matches[0][1]
        used_native_phases: Counter[str] = Counter()
        for route_phase, native_phase in phases.items():
            route_phase = _as_identifier(
                route_phase, f"{prefix}.phases key", maximum=64
            )
            native_phase = _as_identifier(native_phase, f"{prefix}.phases.{route_phase}")
            used_native_phases[native_phase] += 1
            phase_count = available_phases.get(native_phase, 0)
            if phase_count == 0:
                raise BindingError(
                    f"{prefix}: phase '{native_phase}' is not in native quest '{native_quest}'"
                )
            if phase_count != 1:
                raise BindingError(
                    f"{prefix}: phase '{native_phase}' is ambiguous within native quest '{native_quest}'"
                )
        duplicate_phase = next((name for name, count in used_native_phases.items() if count > 1), None)
        if duplicate_phase:
            raise BindingError(
                f"{prefix}: native phase '{duplicate_phase}' is mapped more than once"
            )
    duplicate_id = next((name for name, count in seen_binding_ids.items() if count > 1), None)
    if duplicate_id:
        raise BindingError(f"native quest binding id '{duplicate_id}' appears more than once")
    duplicate_native_quest = next(
        (name for name, count in seen_native_quests.items() if count > 1), None
    )
    if duplicate_native_quest:
        raise BindingError(
            f"native quest '{duplicate_native_quest}' is bound by more than one native route"
        )
    if selected_project_root is not None and map_name is not None:
        map_path = selected_project_root / "data" / "maps" / map_name
        if not map_path.is_dir():
            raise BindingError(
                f"selected custom map directory does not exist: {map_path}"
            )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bindings", type=Path, required=True, help="JSON binding manifest")
    parser.add_argument(
        "--quest-xml",
        type=Path,
        action="append",
        default=[],
        help="compiled quest XML (optional with --project-root; repeatable)",
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        help="custom Workshop project root; rejects compiled XML outside it",
    )
    parser.add_argument(
        "--project",
        help="exact active custom-project identifier (defaults to --project-root name)",
    )
    parser.add_argument(
        "--map",
        dest="map_name",
        help="exact active custom-map identifier",
    )
    args = parser.parse_args(argv)
    try:
        xml_paths = list(args.quest_xml)
        if args.project_root is not None:
            seen = {_path_key(path) for path in xml_paths}
            for path in discover_project_quest_xml(args.project_root):
                if _path_key(path) not in seen:
                    seen.add(_path_key(path))
                    xml_paths.append(path)
        validate(
            args.bindings,
            xml_paths,
            project_root=args.project_root,
            project=args.project,
            map_name=args.map_name,
        )
    except BindingError as exc:
        print(f"quest binding validation failed: {exc}", file=sys.stderr)
        return 2
    print(f"validated {args.bindings} against {len(xml_paths)} compiled quest XML file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
