#!/usr/bin/env python3
"""Fail if any listed gcov file is under the line-coverage floor."""

from __future__ import annotations

import argparse
import os
import sys


def line_coverage(path: str) -> tuple[int, int]:
    executed = 0
    total = 0
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            # gcov format: "    -:  12: comment" / "    3:  13: code" / "#####:  14: missed"
            if ":" not in line:
                continue
            count, rest = line.split(":", 1)
            count = count.strip()
            rest = rest.lstrip()
            if not rest[:1].isdigit():
                continue
            if count in ("-", "====="):
                continue
            total += 1
            if count != "#####" and not count.startswith("-"):
                executed += 1
    return executed, total


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--min", type=float, default=80.0)
    parser.add_argument("gcov_files", nargs="+")
    args = parser.parse_args()

    failed = False
    print(f"{'file':<32} {'lines':>8} {'hit':>8} {'pct':>8}")
    for path in args.gcov_files:
        if not os.path.isfile(path):
            print(f"missing gcov file: {path}", file=sys.stderr)
            failed = True
            continue
        hit, total = line_coverage(path)
        pct = 100.0 * hit / total if total else 100.0
        name = os.path.basename(path).replace(".gcov", "")
        print(f"{name:<32} {total:>8} {hit:>8} {pct:>7.1f}%")
        if pct + 1e-9 < args.min:
            print(f"  FAIL: {name} is below {args.min:.0f}%", file=sys.stderr)
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
