#!/usr/bin/env python3
"""Verify that a loader PE embeds the revision generated for its source."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


HEAD_RE = re.compile(r"^[0-9a-fA-F]{40}$")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    head = args.head.strip().lower()
    if not HEAD_RE.fullmatch(head):
        print("ERROR: --head must be exactly 40 hexadecimal characters", file=sys.stderr)
        return 2
    try:
        payload = args.binary.read_bytes()
    except OSError as error:
        print(f"ERROR: cannot read {args.binary}: {error}", file=sys.stderr)
        return 2
    required = (head.encode("ascii"), head[:12].encode("ascii"),
                args.version.encode("ascii"))
    missing = [value.decode("ascii") for value in required if value not in payload]
    if missing:
        print("ERROR: loader binary is missing provenance: " + ", ".join(missing),
              file=sys.stderr)
        return 1
    if b"commit: unavailable" in payload.lower():
        print("ERROR: loader binary embeds the unavailable commit fallback", file=sys.stderr)
        return 1
    print(f"OK: {args.binary} embeds {head[:12]} and DLUMA_VERSION {args.version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
