#!/usr/bin/env python3
"""The reboot.api.v1 CI gate.

1. The committed C++ matches a fresh protoc-gen-reboot-cpp run (which also enforces contiguity).
2. The committed golden vectors match protoc's encoding of tests/data/*.txtpb.
3. buf lint passes on launcher/schema.
4. buf breaking passes against the newest release tag that has the schema; skipped before the first.

    check_schema.py --protoc <protoc> [--cargo <cargo>] [--buf <buf>] [--against-tag <tag>]
"""

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
API_DIR = os.path.dirname(HERE)
SCHEMA_DIR = os.path.normpath(os.path.join(API_DIR, "..", "..", "schema"))
PROTO_ROOT = os.path.join(SCHEMA_DIR, "proto")
GENERATOR = os.path.join(HERE, "protoc-gen-reboot-cpp", "Cargo.toml")


def run(command, cwd=API_DIR):
    print("+ " + " ".join(command), flush=True)
    return subprocess.run(command, cwd=cwd, check=False).returncode == 0


def git(*args):
    result = subprocess.run(["git", *args], cwd=SCHEMA_DIR, capture_output=True, text=True, check=False)
    return result.stdout.strip() if result.returncode == 0 else None


# The 10.x tags predate the schema, so the newest tag is not necessarily a release of it.
def released_tag(subdir):
    tags = git("tag", "--merged", "HEAD", "--sort=-creatordate") or ""
    for tag in tags.splitlines():
        if git("cat-file", "-e", f"{tag}:{subdir}/buf.yaml") is not None:
            return tag
    return None


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--protoc", default="protoc")
    parser.add_argument("--cargo", default=shutil.which("cargo") or "cargo")
    parser.add_argument("--buf", default=shutil.which("buf"))
    parser.add_argument("--against-tag", help="defaults to the newest tag that contains the schema")
    args = parser.parse_args(argv)

    ok = run([args.cargo, "run", "--quiet", "--release", "--manifest-path", GENERATOR, "--",
              "--protoc", args.protoc, "--proto-root", PROTO_ROOT, "--out", os.path.dirname(API_DIR), "--check"])
    ok &= run([sys.executable, os.path.join(HERE, "make_golden.py"), "--protoc", args.protoc,
               "--proto-root", PROTO_ROOT, "--data-dir", os.path.join(API_DIR, "tests", "data"), "--check"])

    if not args.buf:
        print("buf not found: lint and breaking checks are required in CI", file=sys.stderr)
        return 1
    ok &= run([args.buf, "lint"], cwd=SCHEMA_DIR)

    top = git("rev-parse", "--show-toplevel")
    subdir = os.path.relpath(SCHEMA_DIR, top).replace(os.sep, "/")
    tag = args.against_tag or released_tag(subdir)
    if tag:
        ok &= run([args.buf, "breaking", "--against", f"{top}/.git#tag={tag},subdir={subdir}"], cwd=SCHEMA_DIR)
    else:
        print("no release tag yet: buf breaking skipped")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
