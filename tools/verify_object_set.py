#!/usr/bin/env python3
"""Verify an exact, explicitly declared native object-file set."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--object-dir", required=True, type=Path)
    parser.add_argument(
        "--expected",
        required=True,
        help="Comma-separated object basenames.",
    )
    args = parser.parse_args()

    object_dir = args.object_dir.resolve()
    expected = {
        name.strip().casefold()
        for name in args.expected.split(",")
        if name.strip()
    }
    if not expected:
        raise SystemExit("Object manifest is empty")

    actual = {
        path.name.casefold()
        for path in object_dir.glob("*.obj")
        if path.is_file()
    }
    missing = sorted(expected - actual)
    unexpected = sorted(actual - expected)
    if missing or unexpected:
        raise SystemExit(
            "Object manifest mismatch; "
            f"missing={missing or '<none>'}, "
            f"unexpected={unexpected or '<none>'}"
        )
    print(f"Verified exact {len(actual)}-object set in {object_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
