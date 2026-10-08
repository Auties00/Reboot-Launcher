#!/usr/bin/env python3
"""Encodes each <data-dir>/*.txtpb with protoc into the .bin the golden tests compare against.

    make_golden.py --protoc <protoc> --proto-root <launcher/schema/proto> --data-dir <core/api/tests/data> [--check]

Each .txtpb names its message on a "# proto-message: reboot.api.v1.<Message>" line. Message
fields the sb codec always emits must be written out, even as `field {}`, because protoc only
emits the ones that are set.
"""

import argparse
import glob
import os
import re
import subprocess
import sys


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--protoc", default="protoc")
    parser.add_argument("--proto-root", required=True)
    parser.add_argument("--data-dir", required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)

    protos = sorted(os.path.relpath(p, args.proto_root).replace(os.sep, "/")
                    for p in glob.glob(os.path.join(args.proto_root, "reboot", "api", "v1", "*.proto")))
    stale = []
    for source in sorted(glob.glob(os.path.join(args.data_dir, "*.txtpb"))):
        with open(source, "rb") as stream:
            text = stream.read()
        match = re.search(rb"^# proto-message: (\S+)$", text, re.MULTILINE)
        if not match:
            print(f"{source}: missing '# proto-message:' line", file=sys.stderr)
            return 1
        encoded = subprocess.run([args.protoc, f"-I{args.proto_root}", f"--encode={match.group(1).decode()}", *protos],
                                 input=text, capture_output=True, check=True).stdout
        target = os.path.splitext(source)[0] + ".bin"
        current = None
        if os.path.exists(target):
            with open(target, "rb") as stream:
                current = stream.read()
        if current == encoded:
            continue
        stale.append(os.path.basename(target))
        if not args.check:
            with open(target, "wb") as stream:
                stream.write(encoded)
    if args.check and stale:
        print("golden vectors are out of date: " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
