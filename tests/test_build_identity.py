from __future__ import annotations

import os
import subprocess
import sys
import zipfile

import pytest
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


def _generate(repo: Path, output: Path, *, allow_archive: bool = True):
    command = [sys.executable, str(ROOT / "tools/generate_build_provenance.py"),
               "--repo", str(repo), "--output", str(output)]
    if allow_archive:
        command.append("--allow-source-archive")
    return subprocess.run(command, capture_output=True, text=True)


def test_git_archive_revision_is_embedded(tmp_path: Path) -> None:
    repo = tmp_path / "repo with spaces"
    repo.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.email", "tests@example.invalid"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.name", "DLUMA Tests"], cwd=repo, check=True)
    tools = repo / "tools"
    tools.mkdir()
    for name in (".gitattributes", ".git_archival.txt"):
        (tools / name).write_bytes((ROOT / "tools" / name).read_bytes())
    subprocess.run(["git", "add", "tools"], cwd=repo, check=True)
    subprocess.run(["git", "commit", "-q", "-m", "archive fixture"], cwd=repo, check=True)
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    archive = tmp_path / "source.zip"
    subprocess.run(["git", "archive", "--format=zip", "--output", str(archive), "HEAD"], cwd=repo, check=True)
    source = tmp_path / "source"
    with zipfile.ZipFile(archive) as z:
        z.extractall(source)
    output = tmp_path / "archive.h"
    result = _generate(source, output)
    assert result.returncode == 0, result.stderr
    text = output.read_text(encoding="utf-8")
    assert f'DLUMA_BUILD_GIT_HEAD "{head}"' in text
    assert f'DLUMA_BUILD_GIT_SHORT "{head[:12]}"' in text
    assert 'DLUMA_BUILD_WORKTREE_FINGERPRINT "source-archive"' in text
    worktree = tmp_path / "worktree with spaces"
    subprocess.run(["git", "worktree", "add", "--detach", str(worktree), "HEAD"],
                   cwd=repo, check=True, capture_output=True)
    assert (worktree / ".git").is_file()
    (worktree / "tools/.git_archival.txt").write_text("commit=" + "a" * 40 + "\n", encoding="utf-8")
    checkout_output = tmp_path / "checkout.h"
    result = _generate(worktree, checkout_output)
    assert result.returncode == 0, result.stderr
    assert f'DLUMA_BUILD_GIT_HEAD "{head}"' in checkout_output.read_text(encoding="utf-8")


@pytest.mark.parametrize("metadata", [None, "commit=$Format:%H$\n", "commit=abc\n", "commit=" + "a" * 40 + " extra\n"])
def test_missing_or_invalid_archive_metadata_is_unavailable(tmp_path: Path, metadata: str | None) -> None:
    source = tmp_path / "source"
    (source / "tools").mkdir(parents=True)
    if metadata is not None:
        (source / "tools/.git_archival.txt").write_text(metadata, encoding="utf-8")
    output = tmp_path / "archive.h"
    result = _generate(source, output)
    assert result.returncode == 0, result.stderr
    assert 'DLUMA_BUILD_GIT_HEAD "unavailable"' in output.read_text(encoding="utf-8")


def test_broken_checkout_does_not_fall_back_to_archive_metadata(tmp_path: Path) -> None:
    repo = tmp_path / "checkout"
    (repo / "tools").mkdir(parents=True)
    (repo / ".git").write_text("gitdir: missing\n", encoding="utf-8")
    (repo / "tools/.git_archival.txt").write_text("commit=" + "a" * 40 + "\n", encoding="utf-8")
    result = _generate(repo, tmp_path / "output.h")
    assert result.returncode != 0
    assert "verified Git HEAD is required" in result.stderr


def test_archive_requires_explicit_permission(tmp_path: Path) -> None:
    source = tmp_path / "source"
    source.mkdir()
    result = _generate(source, tmp_path / "output.h", allow_archive=False)
    assert result.returncode != 0
    assert "verified Git HEAD is required" in result.stderr
