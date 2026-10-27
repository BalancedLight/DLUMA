from __future__ import annotations

import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_build_identity_fake_harness() -> None:
    subprocess.run(
        ["cmd.exe", "/d", "/s", "/c", "call tools\\build_build_identity_fake_tests.bat"],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [str(ROOT / "build/build-identity-fake/build_identity_fake_harness.exe")],
        cwd=ROOT,
        check=True,
    )


def test_runtime_identity_is_self_pe_hash_and_startup_visible() -> None:
    runtime = (ROOT / "src/dlw_build_identity.c").read_text(encoding="utf-8")
    core = (ROOT / "src/dlw_build_identity_core.c").read_text(encoding="utf-8")
    exports = (ROOT / "src/dlw_exports.c").read_text(encoding="utf-8")
    project = (ROOT / "DLUMA.vcxproj").read_text(encoding="utf-8")
    path_security = (ROOT / "src/dlw_path_security.c").read_text(encoding="utf-8")

    assert "dluma_get_module_path_checked" in runtime
    assert "GetModuleFileNameW" in path_security
    assert "GetFileInformationByHandle" in runtime
    assert "FILE_SHARE_READ | FILE_SHARE_DELETE" in runtime
    assert "FILE_SHARE_WRITE" not in runtime
    assert "dluma_sha256_final(&context, digest)" in runtime
    assert "disk_timestamp != loaded_timestamp" in runtime
    assert "disk_size_of_image != loaded_size_of_image" in runtime
    assert "DLW_BUILD_IDENTITY_V1" in core
    assert "dluma_build_identity_initialize(g_dluma.self_module)" in exports
    assert "dluma_build_identity_log();" in exports
    assert '"loader_build_git_head"' in runtime
    assert '"loader_build_worktree_fingerprint"' in runtime
    msvc_build = (ROOT / "build_windows_msvc.bat").read_text(encoding="utf-8")
    clang_build = (ROOT / "build_windows_clang.bat").read_text(encoding="utf-8")
    generator = (ROOT / "tools" / "generate_build_provenance.py").read_text(encoding="utf-8")
    verifier = (ROOT / "tools" / "verify_build_provenance.py").read_text(encoding="utf-8")
    assert "msbuild.exe DLUMA.vcxproj" in msvc_build
    assert "generate_build_provenance.py" in clang_build
    assert "DLUMAAllowSourceArchiveProvenance" in project
    for required in ('"git", *args', '"rev-parse"', '"--verify"', '"HEAD"'):
        assert required in generator
    assert "HEAD_RE.fullmatch(head)" in generator
    assert "os.replace" in generator
    assert "--allow-source-archive" in generator
    assert "commit: unavailable" in verifier
    assert "GenerateDlumaBuildProvenance" in project
    assert "generate_build_provenance.py" in project
    prepare = project.split(
        '<Target Name="PrepareDlumaBuildDirectories"', 1
    )[1].split(">", 1)[0]
    assert 'DependsOnTargets="GenerateDlumaBuildProvenance"' in prepare
    generate = project.split(
        '<Target Name="GenerateDlumaBuildProvenance"', 1
    )[1].split("</Target>", 1)[0]
    assert "RemoveDir" not in generate
    assert "DLUMA_BUILD_GIT_SHORT" in runtime
    assert "dluma_build_git_short" in runtime
    assert "dluma_build_identity_git_head" in runtime
    assert '<ClCompile Include="src\\dlw_build_identity.c" />' in project
    assert '<ClCompile Include="src\\dlw_build_identity_core.c" />' in project
    assert '<ClInclude Include="src\\dlw_build_identity.h" />' in project


def test_clean_checkout_build_provenance_reports_not_dirty(tmp_path: Path) -> None:
    repo = tmp_path / "checkout"
    output = tmp_path / "generated" / "dluma_build_provenance.h"
    repo.mkdir()
    git_env = os.environ.copy()
    git_env["GIT_CONFIG_GLOBAL"] = str(tmp_path / "gitconfig")
    subprocess.run(
        [
            "git",
            "config",
            "--global",
            "--add",
            "safe.directory",
            str(repo.resolve()),
        ],
        cwd=repo,
        env=git_env,
        check=True,
    )
    subprocess.run(["git", "init", "-q"], cwd=repo, env=git_env, check=True)
    subprocess.run(
        ["git", "config", "user.email", "tests@example.invalid"],
        cwd=repo,
        env=git_env,
        check=True,
    )
    subprocess.run(
        ["git", "config", "user.name", "DLUMA Tests"],
        cwd=repo,
        env=git_env,
        check=True,
    )
    (repo / "tracked.txt").write_text("clean\n", encoding="utf-8")
    subprocess.run(
        ["git", "add", "tracked.txt"], cwd=repo, env=git_env, check=True
    )
    subprocess.run(
        ["git", "commit", "-q", "-m", "clean fixture"],
        cwd=repo,
        env=git_env,
        check=True,
    )
    command = [
        "python",
        str(ROOT / "tools/generate_build_provenance.py"),
        "--repo",
        str(repo),
        "--output",
        str(output),
    ]
    subprocess.run(command, cwd=ROOT, env=git_env, check=True)
    assert "#define DLUMA_BUILD_WORKTREE_DIRTY 0" in output.read_text(
        encoding="utf-8"
    )
    (repo / "tracked.txt").write_text("dirty\n", encoding="utf-8")
    subprocess.run(command, cwd=ROOT, env=git_env, check=True)
    assert "#define DLUMA_BUILD_WORKTREE_DIRTY 1" in output.read_text(
        encoding="utf-8"
    )


def test_version_and_export_surface_are_unchanged() -> None:
    common = (ROOT / "src/dlw_common.h").read_text(encoding="utf-8")
    exports = (ROOT / "src/dlw_exports.c").read_text(encoding="utf-8")
    assert '#define DLUMA_VERSION "0.4.0"' in common
    assert "DLUMA_GetVersion" in exports
    assert "InitializeGameScriptDLL" in exports
    assert "ShutdownGameScriptDLL" in exports
