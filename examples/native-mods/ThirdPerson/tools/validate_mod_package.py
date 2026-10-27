#!/usr/bin/env python3
"""Validate, lock, or deterministically package a DLUMA API 1.0 mod."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from dluma_mod_package import (
    PackageError,
    create_package,
    validate_lock,
    validate_manifest,
    write_lock,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("validate", "lock"):
        child = subparsers.add_parser(command)
        child.add_argument("mod_dir", type=Path)
        if command == "validate":
            child.add_argument("--require-lock", action="store_true")
    package = subparsers.add_parser("package")
    package.add_argument("mod_dir", type=Path)
    package.add_argument("--out", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "lock":
            result = write_lock(args.mod_dir)
            print(
                json.dumps(
                    {
                        "mod_id": result["mod_id"],
                        "catalog_sha256": result["catalog_sha256"],
                        "artifacts": len(result["artifacts"]),
                    },
                    sort_keys=True,
                )
            )
        elif args.command == "package":
            print(json.dumps(create_package(args.mod_dir, args.out), sort_keys=True))
        else:
            manifest, _ = validate_manifest(args.mod_dir)
            if args.require_lock:
                lock = validate_lock(args.mod_dir)
                artifact_count = len(lock["artifacts"])
            else:
                artifact_count = 0
            print(
                json.dumps(
                    {
                        "api_version": manifest["api_version"],
                        "id": manifest["id"],
                        "language": manifest["language"],
                        "scope": manifest["scope"],
                        "locked_artifacts": artifact_count,
                    },
                    sort_keys=True,
                )
            )
        return 0
    except (OSError, PackageError) as exc:
        print(f"DLUMA package error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
