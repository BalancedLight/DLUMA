"""Deterministic stock DevTools project inventory and strict comparator."""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable


SCHEMA = "dluma.devtools-template-manifest.v1"
DEFAULT_IGNORED_PREFIXES = ("out/",)


@dataclass(frozen=True)
class TemplateComparison:
    added: tuple[str, ...]
    missing: tuple[str, ...]
    modified: tuple[str, ...]

    @property
    def exact(self) -> bool:
        return not (self.added or self.missing or self.modified)


def _relative_name(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _ignored(name: str, ignored_prefixes: Iterable[str]) -> bool:
    folded = name.casefold()
    return any(folded.startswith(prefix.casefold()) for prefix in ignored_prefixes)


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def inventory_template(
    root: Path,
    ignored_prefixes: Iterable[str] = DEFAULT_IGNORED_PREFIXES,
) -> list[dict[str, object]]:
    root = root.resolve(strict=True)
    files: list[dict[str, object]] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        name = _relative_name(path, root)
        if _ignored(name, ignored_prefixes):
            continue
        PurePosixPath(name)
        files.append(
            {
                "path": name,
                "size": path.stat().st_size,
                "sha256": _sha256(path),
            }
        )
    files.sort(key=lambda row: str(row["path"]).casefold())
    return files


def build_manifest(root: Path) -> dict[str, object]:
    return {
        "schema": SCHEMA,
        "template": "DevToolsDefaultProject/example",
        "ignored_prefixes": list(DEFAULT_IGNORED_PREFIXES),
        "files": inventory_template(root),
    }


def load_manifest(path: Path) -> dict[str, object]:
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") != SCHEMA or not isinstance(
        document.get("files"), list
    ):
        raise ValueError("unsupported DevTools template manifest")
    return document


def compare_template(
    root: Path, manifest: dict[str, object]
) -> TemplateComparison:
    ignored = tuple(
        str(value) for value in manifest.get("ignored_prefixes", ())
    )
    expected = {
        str(row["path"]).casefold(): row
        for row in manifest["files"]
    }
    observed_rows = inventory_template(root, ignored)
    observed = {
        str(row["path"]).casefold(): row
        for row in observed_rows
    }
    added = tuple(
        str(observed[key]["path"])
        for key in sorted(observed.keys() - expected.keys())
    )
    missing = tuple(
        str(expected[key]["path"])
        for key in sorted(expected.keys() - observed.keys())
    )
    modified = tuple(
        str(expected[key]["path"])
        for key in sorted(expected.keys() & observed.keys())
        if expected[key]["size"] != observed[key]["size"]
        or expected[key]["sha256"] != observed[key]["sha256"]
    )
    return TemplateComparison(added, missing, modified)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--write", type=Path)
    parser.add_argument("--check", type=Path)
    arguments = parser.parse_args()
    if bool(arguments.write) == bool(arguments.check):
        parser.error("choose exactly one of --write or --check")
    if arguments.write:
        document = build_manifest(arguments.root)
        arguments.write.write_text(
            json.dumps(document, indent=2) + "\n",
            encoding="utf-8",
        )
        return 0
    comparison = compare_template(
        arguments.root, load_manifest(arguments.check)
    )
    if comparison.exact:
        return 0
    print(
        json.dumps(
            {
                "added": comparison.added,
                "missing": comparison.missing,
                "modified": comparison.modified,
            },
            indent=2,
        )
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
