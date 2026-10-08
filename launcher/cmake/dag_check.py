#!/usr/bin/env python3
"""Enforces the package DAG of spec section 4.2 from recorded DEPS and #include lines."""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

FOUNDATION = {"reboot_foundation", "reboot_ports", "reboot_contracts"}
CLIENT_ALLOWED = FOUNDATION | {"reboot_client", "reboot_ipc", "reboot_client_platform",
                               "reboot_os_windows_ipc", "reboot_os_macos_ipc", "reboot_os_linux_ipc"}
FORBIDDEN_CLIENT_LIBS = ("openssl", "libcrypto", "libssl", "curl", "msquic", "archive")
OS_HEADERS = re.compile(
    r"#\s*include\s*<(windows\.h|winsock2\.h|ws2tcpip\.h|winternl\.h|bcrypt\.h|shlobj\.h|unistd\.h|fcntl\.h|"
    r"spawn\.h|dlfcn\.h|pthread\.h|poll\.h|sys/[^>]+|netinet/[^>]+|arpa/[^>]+|mach/[^>]+|mach-o/[^>]+|"
    r"CoreFoundation/[^>]+|Security/[^>]+|linux/[^>]+)>")
REBOOT_INCLUDE = re.compile(r'#\s*include\s*"(reboot/[^"]+)"')
SOURCE_SUFFIXES = {".hpp", ".cpp", ".h", ".mm"}


def owner_of(include):
    """Maps an include path to the target that owns it."""
    if include == "reboot/client.h":
        return "reboot_client"
    parts = include.split("/")
    if len(parts) >= 3 and parts[1] in ("foundation", "ports", "contracts"):
        return "reboot_" + parts[1]
    if len(parts) >= 4 and parts[1].startswith("os_"):
        return "reboot_" + parts[1] + "_" + parts[2]
    if len(parts) >= 3:
        return "reboot_" + parts[1]
    return None


def normalise(dep):
    return dep.replace("::", "_") if dep.startswith("reboot::") else dep


def load_manifest(path):
    targets = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        name, kind, directory, deps, sources = (line.split("|", 4) + [""])[:5]
        targets[name] = {"kind": kind, "dir": Path(directory).resolve(),
                         "deps": [normalise(d) for d in deps.split(";") if d],
                         "sources": [Path(s).resolve() for s in sources.split(";") if s]}
    return targets


def source_files(directory, target_dirs, claimed):
    """Files under `directory` that no deeper target directory and no target's own SOURCES own."""
    for path in directory.rglob("*"):
        if path.suffix not in SOURCE_SUFFIXES or "build" in path.parts or path.resolve() in claimed:
            continue
        owner = max((d for d in target_dirs if path.is_relative_to(d)), key=lambda d: len(d.parts))
        if owner == directory:
            yield path


def is_os_target(name):
    return name.startswith("reboot_os_") or name == "reboot-winhost"


def allowed_os_deps(name):
    allowed = set(FOUNDATION) | {name}
    if name.startswith("reboot_os_macos") or name.startswith("reboot_os_linux"):
        allowed.add("reboot_posix")
    if name in ("reboot_os_windows_platform", "reboot-winhost"):
        allowed.add("reboot_os_windows_win32session")
    return allowed


def closure(targets, start):
    seen, stack = set(), [start]
    while stack:
        current = stack.pop()
        if current in seen:
            continue
        seen.add(current)
        stack.extend(targets.get(current, {}).get("deps", []))
    return seen


def check_client_imports(binary):
    tools = [["objdump", "-p"], ["otool", "-L"], ["readelf", "-d"], ["dumpbin", "/dependents"]]
    for tool in tools:
        if shutil.which(tool[0]):
            output = subprocess.run(tool + [binary], capture_output=True, text=True, check=False).stdout.lower()
            return [f"reboot_client imports {lib}" for lib in FORBIDDEN_CLIENT_LIBS if lib in output]
    return ["no objdump/otool/readelf/dumpbin found to inspect reboot_client imports"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--client")
    args = parser.parse_args()

    targets = load_manifest(args.manifest)
    root = Path(args.root).resolve()
    target_dirs = {info["dir"] for info in targets.values()}
    claimed = {source for info in targets.values() for source in info["sources"]}
    errors = []

    for name, info in targets.items():
        declared = {d for d in info["deps"] if d.startswith("reboot_")}
        is_foundation = name in FOUNDATION
        if is_foundation:
            allowed = set(FOUNDATION)
        elif info["kind"] == "test":
            allowed = set(FOUNDATION) | {name, "reboot_testing"} | declared
        elif is_os_target(name):
            allowed = allowed_os_deps(name)
        else:
            allowed = set(FOUNDATION) | {name} | declared
            for dep in declared:
                if dep.startswith("reboot_os_") or dep == "reboot_posix":
                    errors.append(f"{name}: domain package declares OS dependency {dep}")
        if is_os_target(name) and info["kind"] != "test":
            for dep in declared - allowed:
                errors.append(f"{name}: OS package declares {dep}")

        files = info["sources"] or source_files(info["dir"], target_dirs, claimed)
        for path in files:
            if path.suffix not in SOURCE_SUFFIXES:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            rel = path.relative_to(root) if path.is_relative_to(root) else path
            in_tests = "tests" in path.relative_to(info["dir"]).parts
            for include in REBOOT_INCLUDE.findall(text):
                owner = owner_of(include)
                if owner is None or owner in allowed or (in_tests and owner == "reboot_testing"):
                    continue
                errors.append(f"{rel}: includes {include} ({owner} is not a dependency of {name})")
            core_file = "core" in rel.parts and "posix" not in rel.parts
            if core_file and rel.as_posix() != "core/foundation/src/random_os.cpp":
                for header in OS_HEADERS.findall(text):
                    errors.append(f"{rel}: includes OS header <{header}>")

    if "reboot_client" in targets:
        for dep in closure(targets, "reboot_client"):
            if dep.startswith("reboot_") and dep not in CLIENT_ALLOWED:
                errors.append(f"reboot_client reaches {dep}")
            if any(lib in dep.lower() for lib in FORBIDDEN_CLIENT_LIBS):
                errors.append(f"reboot_client links {dep}")
    if args.client:
        errors.extend(check_client_imports(args.client))

    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
