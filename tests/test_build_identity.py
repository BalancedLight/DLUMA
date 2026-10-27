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
