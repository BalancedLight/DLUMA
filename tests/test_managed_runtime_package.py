from __future__ import annotations

import hashlib
import json
from pathlib import Path

import pytest

from tools import build_managed_runtime
from tools import dotnet_runtime_source


RUNTIME_VERSION = "10.0.13"


def _write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _fake_inputs(tmp_path: Path) -> tuple[Path, Path, Path, Path, Path]:
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
    editor_theme = tmp_path / "editor-theme"
    for name in build_managed_runtime.EDITOR_THEME_ARTIFACTS:
        _write(editor_theme / name, name.encode("ascii"))
    return dotnet, managed, quest_graph, editor_theme, catalog_hash


def _assemble(tmp_path: Path) -> tuple[Path, dict[str, object]]:
    dotnet, managed, quest_graph, editor_theme, catalog_hash = _fake_inputs(tmp_path)
    output = tmp_path / "package"
    sources = build_managed_runtime.collect_sources(
        dotnet, managed, RUNTIME_VERSION, quest_graph, editor_theme
    )
    manifest = build_managed_runtime.build_manifest(
        sources,
        RUNTIME_VERSION,
        build_managed_runtime.read_catalog_hash(catalog_hash),
    )
    build_managed_runtime.write_runtime(output, sources, manifest)
    return output, manifest


@pytest.mark.parametrize("include_splash", [False, True])
def test_optional_startup_image_remains_manifest_verified(tmp_path: Path, include_splash: bool) -> None:
    dotnet, managed, quest_graph, editor_theme, catalog_hash = _fake_inputs(tmp_path)
    if include_splash:
        _write(editor_theme / "startup.gif", b"GIF89a")
    sources = build_managed_runtime.collect_sources(
        dotnet, managed, RUNTIME_VERSION, quest_graph, editor_theme
    )
    manifest = build_managed_runtime.build_manifest(
        sources, RUNTIME_VERSION, build_managed_runtime.read_catalog_hash(catalog_hash)
    )
    output = tmp_path / "package"
    build_managed_runtime.write_runtime(output, sources, manifest)
    build_managed_runtime.verify_runtime(output)
    splash = output / "dluma_runtime/editor/startup.gif"
    assert splash.exists() == include_splash
    if include_splash:
        splash.write_bytes(b"GIF89b")
        with pytest.raises(ValueError, match="SHA-256 changed"):
            build_managed_runtime.verify_runtime(output)


def test_runtime_cache_survives_installed_runtime_removal(tmp_path: Path) -> None:
    installed, _, _, _, _ = _fake_inputs(tmp_path)
    cache_parent = tmp_path / "cache"
    cached = dotnet_runtime_source.prepare_runtime(RUNTIME_VERSION, [installed], cache_parent)
    installed.rename(tmp_path / "removed-sdk")
    assert dotnet_runtime_source.prepare_runtime(RUNTIME_VERSION, [], cache_parent) == cached
    assert dotnet_runtime_source.complete(cached, RUNTIME_VERSION)


def test_runtime_cache_restores_missing_runtime_once(tmp_path: Path, monkeypatch) -> None:
    installed, _, _, _, _ = _fake_inputs(tmp_path)
    restored = []

    def download(destination: Path, version: str) -> None:
        import shutil
        restored.append(version)
        shutil.copytree(installed, destination, dirs_exist_ok=True)

    monkeypatch.setattr(dotnet_runtime_source, "download_runtime", download)
    first = dotnet_runtime_source.prepare_runtime(RUNTIME_VERSION, [], tmp_path / "cache")
    assert dotnet_runtime_source.prepare_runtime(RUNTIME_VERSION, [], tmp_path / "cache") == first
    assert restored == [RUNTIME_VERSION]


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
    assert "editor/dluma_editor_theme_system.dll" in paths
    assert "editor/Dluma.EditorTheme.Framework.dll" in paths
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

    data["files"][0]["path"] = "editor/quest_extensions.edf"
    manifest_path.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(ValueError, match="generated Editor data"):
        build_managed_runtime.verify_runtime(output)

    for mutable in ("managed_mods.ini", "managed_mods.ini.tmp.42"):
        data["files"][0]["path"] = mutable
        manifest_path.write_text(json.dumps(data), encoding="utf-8")
        with pytest.raises(ValueError, match="managed settings"):
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
