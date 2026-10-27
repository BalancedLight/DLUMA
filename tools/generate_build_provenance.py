#!/usr/bin/env python3
"""Generate the loader's compile-time Git provenance header.

This runs only on the build host.  The game process never invokes Git and
cannot silently fall back to an unknown revision in a normal checkout build.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


HEAD_RE = re.compile(r"^[0-9a-fA-F]{40}$")


def run_git(repo: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", *args], cwd=repo, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False,
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr.strip() or "git command failed")
    return completed.stdout


def source_archive_header() -> tuple[str, str, int, str]:
    return "unavailable", "unavailable", 0, "source-archive"


def checkout_header(repo: Path) -> tuple[str, str, int, str]:
    head = run_git(repo, "rev-parse", "--verify", "HEAD").strip()
    if not HEAD_RE.fullmatch(head):
        raise RuntimeError("git HEAD is not exactly 40 hexadecimal characters")
    status = run_git(repo, "status", "--porcelain=v1", "--untracked-files=all")
    dirty = 1 if status else 0
    # The status digest is bounded, deterministic, and never embeds an
    # unbounded path/diff into the PE.  It is diagnostic provenance only.
    fingerprint = "git-sha256:" + hashlib.sha256(
        status.encode("utf-8")
    ).hexdigest()[:16]
    return head.lower(), head.lower()[:12], dirty, fingerprint


def render(head: str, short: str, dirty: int, fingerprint: str) -> str:
    return (
        "#ifndef DLUMA_BUILD_PROVENANCE_H\n"
        "#define DLUMA_BUILD_PROVENANCE_H\n"
        f'#define DLUMA_BUILD_GIT_HEAD "{head}"\n'
        f'#define DLUMA_BUILD_GIT_SHORT "{short}"\n'
        f"#define DLUMA_BUILD_WORKTREE_DIRTY {dirty}\n"
        f'#define DLUMA_BUILD_WORKTREE_FINGERPRINT "{fingerprint}"\n'
        "#endif\n"
    )


def write_if_changed(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8")
    try:
        if path.read_bytes() == encoded:
            return
    except FileNotFoundError:
        pass
    with tempfile.NamedTemporaryFile(
        mode="wb", dir=path.parent, prefix=path.name + ".", delete=False
    ) as handle:
        handle.write(encoded)
        temporary = Path(handle.name)
    try:
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-source-archive", action="store_true")
    args = parser.parse_args()
    try:
        values = checkout_header(args.repo.resolve())
    except (OSError, RuntimeError) as error:
        if not args.allow_source_archive:
            print(
                f"ERROR: verified Git HEAD is required for this build: {error}",
                file=sys.stderr,
            )
            return 1
        values = source_archive_header()
    write_if_changed(args.output.resolve(), render(*values))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
