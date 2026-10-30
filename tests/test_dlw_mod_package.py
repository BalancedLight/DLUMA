import hashlib
import json
import os
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

from tools.dluma_mod_package import (
    COMPATIBLE_SCOPE_CATALOG_SHA256,
    IMMEDIATELY_PREVIOUS_CATALOG_SHA256,
    LOCK_NAME,
    PackageError,
    build_lock,
    canonical_lock_bytes,
    create_package,
    validate_lock,
    validate_manifest,
    write_lock,
)


ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "tools" / "validate_mod_package.py"


def create_mod(
    tmp_path: Path,
    *,
    language: str = "lua",
    scope: str = "global",
    mod_id: str = "example.parity",
) -> Path:
    roots = {
        "lua": tmp_path / "data/scripts/lua",
        "csharp": tmp_path / "mods/csharp",
        "native": tmp_path / "mods/native",
    }
    if scope == "map":
        mod_dir = roots[language] / "maps" / "TestMap" / mod_id
    else:
        mod_dir = roots[language] / scope / mod_id
    mod_dir.mkdir(parents=True)
    entries = {
        "lua": "main.lua",
        "csharp": "Example.Mod.dll",
        "native": "Example.mod",
    }
    entry = entries[language]
    (mod_dir / entry).write_bytes(b"approved entry bytes\n")
    manifest = {
        "$schema": "mod.dluma.schema.json",
        "schema": "dluma.mod.v1",
        "api_version": "1.0",
        "id": mod_id,
        "name": "Parity Example",
        "version": "1.0.0",
        "description": "Test package",
        "author": "DLUMA",
        "language": language,
        "scope": scope,
        "entry": entry,
        "load_priority": 0,
        "access": ["world.query", "events.sensor", "configuration.read"],
    }
    if language == "csharp":
        manifest["entry_type"] = "Example.Mod.Entry"
    if scope == "map":
        manifest["map"] = "TestMap"
    (mod_dir / "mod.dluma.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    return mod_dir


@pytest.mark.parametrize("language", ["lua", "csharp", "native"])
@pytest.mark.parametrize("scope", ["global", "project", "map"])
def test_shared_manifest_accepts_all_languages_and_scopes(
    tmp_path, language, scope
):
    mod_dir = create_mod(tmp_path, language=language, scope=scope)
    manifest, raw = validate_manifest(mod_dir)
    assert manifest["language"] == language
    assert manifest["scope"] == scope
    assert raw == (mod_dir / "mod.dluma.json").read_bytes()


def test_dual_host_quest_phase_manifest(tmp_path):
    mod_dir = create_mod(tmp_path, language="native")
    manifest = json.loads((mod_dir / "mod.dluma.json").read_text(encoding="utf-8"))
    manifest["hosts"] = ["player", "editor"]
    manifest["access"].extend(["quest.phase.handle", "editor.quest.extend"])
    manifest["quest_phase_handlers"] = [
        {
            "id": "example.fade",
            "command_alias": "fade-example",
            "description": "Runs the example fade provider.",
        }
    ]
    (mod_dir / "mod.dluma.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    parsed, _ = validate_manifest(mod_dir)
    assert parsed["hosts"] == ["player", "editor"]
    assert parsed["quest_phase_handlers"][0]["id"] == "example.fade"


def test_phase_declarations_reject_csharp_missing_access_and_duplicate_alias(tmp_path):
    mod_dir = create_mod(tmp_path, language="csharp")
    manifest_path = mod_dir / "mod.dluma.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["quest_phase_handlers"] = [
        {"id": "example.one", "description": "x"}
    ]
    manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
    with pytest.raises(PackageError, match="C# quest phase handlers"):
        validate_manifest(mod_dir)

    native_dir = create_mod(tmp_path / "native", language="native", mod_id="example.native")
    native_path = native_dir / "mod.dluma.json"
    native = json.loads(native_path.read_text(encoding="utf-8"))
    native["quest_phase_handlers"] = [
        {"id": "example.one", "command_alias": "same", "description": "x"},
        {"id": "example.two", "command_alias": "same", "description": "y"},
    ]
    native_path.write_text(json.dumps(native) + "\n", encoding="utf-8")
    with pytest.raises(PackageError, match="require quest.phase.handle"):
        validate_manifest(native_dir)
    native["access"].append("quest.phase.handle")
    native_path.write_text(json.dumps(native) + "\n", encoding="utf-8")
    with pytest.raises(PackageError, match="aliases"):
        validate_manifest(native_dir)

    native["quest_phase_handlers"] = [
        {"id": "example.one", "description": "x"}
    ]
    native["hosts"] = ["editor"]
    native["access"].append("editor.quest.extend")
    native_path.write_text(json.dumps(native) + "\n", encoding="utf-8")
    with pytest.raises(PackageError, match="require the player host"):
        validate_manifest(native_dir)


def test_folder_derived_scope_and_language_must_match(tmp_path):
    mod_dir = create_mod(tmp_path)
    wrong = tmp_path / "mods/native/global/example.parity"
    wrong.parent.mkdir(parents=True)
    mod_dir.rename(wrong)
    with pytest.raises(PackageError, match="folder-derived"):
        validate_manifest(wrong)


def test_lock_binds_manifest_entry_dependency_configuration_and_companion(tmp_path):
    mod_dir = create_mod(tmp_path, language="csharp")
    (mod_dir / "Dependency.dll").write_bytes(b"dependency")
    (mod_dir / "config.schema.json").write_text("{}\n", encoding="utf-8")
    (mod_dir / "Audio.pak").write_bytes(b"pak")
    manifest = json.loads((mod_dir / "mod.dluma.json").read_text(encoding="utf-8"))
    manifest["configuration"] = {"schema": "config.schema.json"}
    manifest["companion_content"] = [{"id": "audio", "path": "Audio.pak"}]
    (mod_dir / "mod.dluma.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    lock = write_lock(mod_dir)
    by_path = {item["path"]: item for item in lock["artifacts"]}
    assert by_path["mod.dluma.json"]["kind"] == "manifest"
    assert by_path["Example.Mod.dll"]["kind"] == "entry"
    assert by_path["Dependency.dll"]["kind"] == "assembly"
    assert by_path["config.schema.json"]["kind"] == "configuration"
    assert by_path["Audio.pak"]["kind"] == "companion"
    assert validate_lock(mod_dir) == lock


def test_changed_reviewed_bytes_fail_closed(tmp_path):
    mod_dir = create_mod(tmp_path)
    write_lock(mod_dir)
    (mod_dir / "main.lua").write_bytes(b"different bytes\n")
    with pytest.raises(PackageError, match="lock does not match exact package bytes"):
        validate_lock(mod_dir)


@pytest.mark.parametrize("compatible_catalog", [
    IMMEDIATELY_PREVIOUS_CATALOG_SHA256,
    COMPATIBLE_SCOPE_CATALOG_SHA256,
])
def test_only_public_predecessor_catalog_is_compatible(tmp_path, compatible_catalog):
    mod_dir = create_mod(tmp_path, language="native")
    manifest_path = mod_dir / "mod.dluma.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["hosts"] = ["player", "editor"]
    manifest["access"].extend(["quest.phase.handle", "editor.quest.extend"])
    manifest["quest_phase_handlers"] = [
        {
            "id": "example.compatible",
            "description": "Exercises the complete public predecessor contract.",
        }
    ]
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    lock = write_lock(mod_dir)
    lock["catalog_sha256"] = compatible_catalog
    (mod_dir / LOCK_NAME).write_bytes(canonical_lock_bytes(lock))
    assert validate_lock(mod_dir)["catalog_sha256"] == (
        compatible_catalog
    )

    lock["catalog_sha256"] = (
        "E3AAF26AA9901C38F8FED3290623FBBDEEBA71753D341D5471392FD67C174A7F"
    )
    (mod_dir / LOCK_NAME).write_bytes(canonical_lock_bytes(lock))
    with pytest.raises(PackageError, match="lock does not match exact package bytes"):
        validate_lock(mod_dir)


def test_new_dependency_invalidates_lock(tmp_path):
    mod_dir = create_mod(tmp_path)
    write_lock(mod_dir)
    (mod_dir / "late.lua").write_text("return {}\n", encoding="utf-8")
    with pytest.raises(PackageError, match="late.lua"):
        validate_lock(mod_dir)


def test_duplicate_json_property_is_rejected(tmp_path):
    mod_dir = create_mod(tmp_path)
    raw = (mod_dir / "mod.dluma.json").read_text(encoding="utf-8")
    raw = raw.replace('"name": "Parity Example",', '"name": "A", "name": "B",')
    (mod_dir / "mod.dluma.json").write_text(raw, encoding="utf-8")
    with pytest.raises(PackageError, match="duplicate JSON property"):
        validate_manifest(mod_dir)


@pytest.mark.parametrize(
    "entry",
    [
        "../main.lua",
        "C:/main.lua",
        "/main.lua",
        "CON.lua",
        "folder./main.lua",
        "main.lua ",
        "bad\u0000.lua",
    ],
)
def test_ambiguous_or_escaping_paths_are_rejected(tmp_path, entry):
    mod_dir = create_mod(tmp_path)
    manifest = json.loads((mod_dir / "mod.dluma.json").read_text(encoding="utf-8"))
    manifest["entry"] = entry
    (mod_dir / "mod.dluma.json").write_text(
        json.dumps(manifest) + "\n", encoding="utf-8"
    )
    with pytest.raises(PackageError):
        validate_manifest(mod_dir)


def test_hard_linked_artifact_is_rejected(tmp_path):
    mod_dir = create_mod(tmp_path)
    linked = tmp_path / "linked.lua"
    try:
        os.link(mod_dir / "main.lua", linked)
    except OSError as exc:
        pytest.skip(f"hard links unavailable: {exc}")
    with pytest.raises(PackageError, match="hard-linked"):
        build_lock(mod_dir)


def test_deterministic_package_contains_lock_and_exact_files(tmp_path):
    mod_dir = create_mod(tmp_path)
    lock = write_lock(mod_dir)
    first = tmp_path / "first.dlumamod"
    second = tmp_path / "second.dlumamod"
    first_result = create_package(mod_dir, first)
    second_result = create_package(mod_dir, second)
    assert first.read_bytes() == second.read_bytes()
    assert first_result["sha256"] == second_result["sha256"]
    with zipfile.ZipFile(first) as archive:
        assert archive.namelist() == sorted(
            [item["path"] for item in lock["artifacts"]] + [LOCK_NAME],
            key=str.casefold,
        )


def test_cli_lock_and_validate_round_trip(tmp_path):
    mod_dir = create_mod(tmp_path)
    locked = subprocess.run(
        [sys.executable, str(CLI), "lock", str(mod_dir)],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert locked.returncode == 0, locked.stderr
    validated = subprocess.run(
        [
            sys.executable,
            str(CLI),
            "validate",
            str(mod_dir),
            "--require-lock",
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert validated.returncode == 0, validated.stderr
    assert json.loads(validated.stdout)["locked_artifacts"] == 2


def test_lock_manifest_hash_matches_exact_bytes(tmp_path):
    mod_dir = create_mod(tmp_path)
    lock = write_lock(mod_dir)
    assert lock["manifest_sha256"] == hashlib.sha256(
        (mod_dir / "mod.dluma.json").read_bytes()
    ).hexdigest().upper()
    assert (mod_dir / LOCK_NAME).read_bytes() == canonical_lock_bytes(lock)
