from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

from tools import build_managed_runtime


RUNTIME_VERSION = "10.0.13"


def _write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _fake_inputs(tmp_path: Path) -> tuple[Path, Path, Path, Path]:
    dotnet = tmp_path / "dotnet"
    _write(dotnet / "dotnet.exe", b"dotnet")
    _write(
        dotnet / "host" / "fxr" / RUNTIME_VERSION / "hostfxr.dll",
        b"hostfxr",
    )
    _write(
        dotnet
        / "shared"
        / "Microsoft.NETCore.App"
        / RUNTIME_VERSION
        / "coreclr.dll",
        b"coreclr",
    )
    _write(
        dotnet
        / "shared"
        / "Microsoft.NETCore.App"
        / RUNTIME_VERSION
        / "hostpolicy.dll",
        b"hostpolicy",
    )
    _write(
        dotnet
        / "shared"
        / "Microsoft.NETCore.App"
        / RUNTIME_VERSION
        / "System.Private.CoreLib.dll",
        b"corelib",
    )
    _write(
        dotnet
        / "shared"
        / "Microsoft.WindowsDesktop.App"
        / RUNTIME_VERSION
        / "PresentationFramework.dll",
        b"wpf",
    )
    _write(dotnet / "LICENSE.txt", b"license")
    _write(dotnet / "ThirdPartyNotices.txt", b"notices")

    managed = tmp_path / "managed"
    for name in (
        "Dluma.Managed.Bootstrap.dll",
        "Dluma.Managed.Bootstrap.deps.json",
        "Dluma.Managed.Bootstrap.runtimeconfig.json",
        "Dluma.Managed.Sdk.dll",
        "Dluma.Managed.Sdk.xml",
    ):
        _write(managed / name, name.encode("ascii"))

    catalog_hash = tmp_path / "catalog.sha256"
    catalog_hash.write_text("AB" * 32 + "\n", encoding="ascii")
    quest_graph = tmp_path / "quest-graph"
    for name in build_managed_runtime.QUEST_GRAPH_ARTIFACTS:
        _write(
            quest_graph.joinpath(*Path(name).parts),
            name.encode("ascii"),
        )
    return dotnet, managed, quest_graph, catalog_hash


def _assemble(tmp_path: Path) -> tuple[Path, dict[str, object]]:
    dotnet, managed, quest_graph, catalog_hash = _fake_inputs(tmp_path)
    output = tmp_path / "package"
    sources = build_managed_runtime.collect_sources(
        dotnet, managed, RUNTIME_VERSION, quest_graph
    )
    manifest = build_managed_runtime.build_manifest(
        sources,
        RUNTIME_VERSION,
        build_managed_runtime.read_catalog_hash(catalog_hash),
    )
    build_managed_runtime.write_runtime(output, sources, manifest)
    return output, manifest


def test_private_runtime_manifest_is_deterministic_and_exact(tmp_path: Path) -> None:
    output, manifest = _assemble(tmp_path)

    verified = build_managed_runtime.verify_runtime(output, manifest)

    assert verified == manifest
    assert manifest["schema"] == "dluma.runtime-tree.v1"
    assert manifest["api_version"] == "1.0"
    assert manifest["catalog_sha256"] == "AB" * 32
    paths = [record["path"] for record in manifest["files"]]
    assert paths == sorted(paths, key=str.casefold)
    assert (
        "dotnet/host/fxr/10.0.13/hostfxr.dll"
        in paths
    )
    assert (
        "dotnet/shared/Microsoft.NETCore.App/10.0.13/coreclr.dll"
        in paths
    )
    assert (
        "dotnet/shared/Microsoft.WindowsDesktop.App/10.0.13/"
        "PresentationFramework.dll"
        in paths
    )
    assert "dotnet/dotnet.exe" in paths
    assert "quest-graph/Dluma.QuestGraph.exe" in paths
    assert "quest-graph/Nodify.dll" in paths
    assert "quest-graph/Licenses/Nodify-MIT.txt" in paths
    assert "managed/Dluma.Managed.Bootstrap.dll" in paths
    assert "managed/Dluma.Managed.Sdk.dll" in paths
    manifest_bytes = (
        output / "dluma_runtime" / "runtime-manifest.json"
    ).read_bytes()
    assert manifest_bytes.endswith(b"\n")
    assert json.loads(manifest_bytes) == manifest


def test_runtime_verification_rejects_changed_bytes(tmp_path: Path) -> None:
    output, manifest = _assemble(tmp_path)
    target = (
        output
        / "dluma_runtime"
        / "dotnet"
        / "host"
        / "fxr"
        / RUNTIME_VERSION
        / "hostfxr.dll"
    )
    target.write_bytes(b"changed")

    with pytest.raises(ValueError, match="size changed|SHA-256 changed"):
        build_managed_runtime.verify_runtime(output, manifest)


def test_runtime_verification_rejects_unlisted_and_missing_files(
    tmp_path: Path,
) -> None:
    output, manifest = _assemble(tmp_path)
    _write(output / "dluma_runtime" / "unlisted.dll", b"unlisted")
    with pytest.raises(ValueError, match="unexpected"):
        build_managed_runtime.verify_runtime(output, manifest)

    (output / "dluma_runtime" / "unlisted.dll").unlink()
    missing = (
        output
        / "dluma_runtime"
        / "managed"
        / "Dluma.Managed.Sdk.dll"
    )
    missing.unlink()
    with pytest.raises(ValueError, match="is missing"):
        build_managed_runtime.verify_runtime(output, manifest)


def test_runtime_verification_rejects_manifest_path_escape(
    tmp_path: Path,
) -> None:
    output, _ = _assemble(tmp_path)
    manifest_path = output / "dluma_runtime" / "runtime-manifest.json"
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    data["files"][0]["path"] = "../escape.dll"
    manifest_path.write_text(json.dumps(data), encoding="utf-8")

    with pytest.raises(ValueError, match="unsafe path"):
        build_managed_runtime.verify_runtime(output)


def test_manifest_hashes_are_sha256(tmp_path: Path) -> None:
    output, manifest = _assemble(tmp_path)
    record = next(
        item
        for item in manifest["files"]
        if item["path"] == "managed/Dluma.Managed.Sdk.dll"
    )
    expected = hashlib.sha256(
        (output / "dluma_runtime" / record["path"]).read_bytes()
    ).hexdigest().upper()
    assert record["sha256"] == expected
