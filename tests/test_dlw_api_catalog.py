import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "api" / "generated" / "dluma_api_catalog.json"
GENERATOR = ROOT / "tools" / "generate_api.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("dluma_generate_api", GENERATOR)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def canonical_json(value):
    return json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")


def test_generated_api_artifacts_are_current():
    completed = subprocess.run(
        [sys.executable, str(GENERATOR), "--check"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    assert completed.returncode == 0, completed.stderr


def test_catalog_hash_covers_the_complete_language_neutral_contract():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    expected = catalog.pop("catalog_sha256")
    actual = hashlib.sha256(canonical_json(catalog)).hexdigest().upper()
    assert actual == expected
    assert (ROOT / "api/generated/dluma_api_catalog.sha256").read_text(
        encoding="utf-8"
    ).strip() == expected


def test_every_operation_has_all_language_adapters_and_shared_policy():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    capability_names = {item["name"] for item in catalog["capabilities"]}
    access_names = {item["name"] for item in catalog["access"]}
    operation_ids = set()
    numeric_ids = set()
    for domain in catalog["domains"]:
        for operation in domain["operations"]:
            assert operation["id"] not in operation_ids
            assert operation["numeric_id"] not in numeric_ids
            operation_ids.add(operation["id"])
            numeric_ids.add(operation["numeric_id"])
            assert set(operation["adapters"]) == {
                "core",
                "lua",
                "csharp",
                "c",
                "cpp",
            }
            assert operation["capability"] in capability_names
            assert set(operation["access"]) <= access_names
            assert operation["errors"]
            assert operation["tests"]
            assert operation["scopes"]


def test_gated_and_planned_operations_declare_every_language_adapter():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    gated = [
        operation
        for domain in catalog["domains"]
        for operation in domain["operations"]
        if operation["status"] in {"gated", "planned"}
    ]
    assert gated
    for operation in gated:
        for language in ("lua", "csharp", "c", "cpp"):
            assert language in operation["adapters"]


def test_generated_c_header_uses_length_aware_strings_and_size_prefixed_tables():
    header = (ROOT / "sdk/c/dluma_api.h").read_text(encoding="utf-8")
    assert "typedef struct DlumaStringView" in header
    assert "uint64_t size;" in header
    assert "typedef struct DlumaStringBuffer" in header
    assert "uint64_t required;" in header
    assert "typedef struct DlumaObjectHandle" in header
    assert "DLW_NATIVE_MOD_ABI_V2" in header
    assert "DLUMANativeMod_GetApiV2" in header
    assert "DlumaNativeModApiV2" in header
    assert "uint32_t struct_size;" in header
    assert "uint32_t version;" in header
    assert "uint32_t capabilities" not in header


def test_generated_catalog_parity_for_lua_csharp_workbench_and_docs():
    catalog = json.loads(CATALOG.read_text(encoding="utf-8"))
    lua = json.loads(
        (ROOT / "api/generated/lua_api.json").read_text(encoding="utf-8")
    )
    extension = json.loads(
        (
            ROOT
            / "vscode-dying-light-lua-intellisense/src/generated-api.json"
        ).read_text(encoding="utf-8")
    )
    workbench = json.loads(
        (
            ROOT / "workbench/Dluma.Workbench/Assets/dluma_api_catalog.json"
        ).read_text(encoding="utf-8")
    )
    csharp = (
        ROOT / "sdk/dotnet/Dluma.Managed.Sdk/Dluma.Generated.g.cs"
    ).read_text(encoding="utf-8")
    docs = (ROOT / "docs/api/1.0/API_REFERENCE.md").read_text(encoding="utf-8")
    assert lua == extension
    assert workbench == catalog
    assert lua["catalog_sha256"] == catalog["catalog_sha256"]
    assert catalog["catalog_sha256"] in csharp
    assert catalog["catalog_sha256"] in docs
    assert len(lua["operations"]) == sum(
        len(domain["operations"]) for domain in catalog["domains"]
    )


def test_duplicate_operation_id_is_rejected(tmp_path, monkeypatch):
    module = load_generator()
    copied = tmp_path / "catalog"
    shutil.copytree(ROOT / "api/catalog", copied)
    world_path = copied / "world.json"
    world = json.loads(world_path.read_text(encoding="utf-8"))
    world["operations"][1]["id"] = world["operations"][0]["id"]
    world_path.write_text(json.dumps(world), encoding="utf-8")
    monkeypatch.setattr(module, "CATALOG_DIR", copied)
    with pytest.raises(module.CatalogError, match="duplicate operation ID"):
        module.load_catalog()


def test_unknown_record_type_is_rejected(tmp_path, monkeypatch):
    module = load_generator()
    copied = tmp_path / "catalog"
    shutil.copytree(ROOT / "api/catalog", copied)
    world_path = copied / "world.json"
    world = json.loads(world_path.read_text(encoding="utf-8"))
    world["operations"][0]["result"] = "DlumaMissingResult"
    world_path.write_text(json.dumps(world), encoding="utf-8")
    monkeypatch.setattr(module, "CATALOG_DIR", copied)
    with pytest.raises(module.CatalogError, match="unknown result record"):
        module.load_catalog()


def test_missing_implemented_adapter_is_rejected(tmp_path, monkeypatch):
    module = load_generator()
    copied = tmp_path / "catalog"
    shutil.copytree(ROOT / "api/catalog", copied)
    core_path = copied / "core.json"
    core = json.loads(core_path.read_text(encoding="utf-8"))
    core["operations"][0]["adapters"]["core"] = ""
    core_path.write_text(json.dumps(core), encoding="utf-8")
    monkeypatch.setattr(module, "CATALOG_DIR", copied)
    with pytest.raises(module.CatalogError, match="needs a core adapter"):
        module.load_catalog()


def test_incompatible_service_table_reorder_is_rejected(tmp_path, monkeypatch):
    module = load_generator()
    copied = tmp_path / "catalog"
    shutil.copytree(ROOT / "api/catalog", copied)
    world_path = copied / "world.json"
    world = json.loads(world_path.read_text(encoding="utf-8"))
    world["operations"][0], world["operations"][1] = (
        world["operations"][1],
        world["operations"][0],
    )
    world["operations"][0]["ordinal"] = 1
    world["operations"][1]["ordinal"] = 2
    world_path.write_text(json.dumps(world), encoding="utf-8")
    monkeypatch.setattr(module, "CATALOG_DIR", copied)
    with pytest.raises(module.CatalogError, match="incompatible table change"):
        module.load_catalog()
