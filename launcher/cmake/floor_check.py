#!/usr/bin/env python3
"""Fails on facilities outside the MSVC / Apple Clang libc++ / GCC 15 floor."""

import re
import sys
from pathlib import Path

BANNED = [
    (re.compile(r"#\s*include\s*<(inplace_vector|flat_map|flat_set|stop_token)>"), "banned header"),
    (re.compile(r"\bstd::(inplace_vector|flat_map|flat_set|jthread|stop_token|stop_source|stop_callback)\b"),
     "banned standard facility"),
    (re.compile(r"\bstd::execution\b"), "std::execution"),
    (re.compile(r"^\s*import\s+std\b", re.MULTILINE), "import std"),
    (re.compile(r"^\s*(export\s+)?module\b", re.MULTILINE), "named module"),
    (re.compile(r"^\s*(export\s+)?import\s+[\w.:<\"]", re.MULTILINE), "module import"),
]
SUFFIXES = {".hpp", ".cpp", ".h", ".mm"}


def main(directories):
    errors = []
    for directory in map(Path, directories):
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if path.suffix not in SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for pattern, what in BANNED:
                for match in pattern.finditer(text):
                    line = text.count("\n", 0, match.start()) + 1
                    errors.append(f"{path}:{line}: {what}: {match.group(0).strip()}")
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
