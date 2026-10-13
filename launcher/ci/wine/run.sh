#!/bin/sh
# Runs core-linux/runner's Wine gate in the launcher build image: the Windows-built reboot-winhost.exe
# and reboot-fake-game.exe under the pinned Kron4ek Wine. --init reaps what Wine leaves orphaned.
#   run.sh <launcher build image> <extracted Kron4ek Wine> <directory holding both exes>
# The build image is linux.Dockerfile's "build" stage. With every case skipped the binary exits 4,
# so a missing input fails the job instead of passing it.
set -eu
image=$1
wine=$(cd "$2" && pwd)
payload=$(cd "$3" && pwd)
tests=/src/launcher/build/linux-gcc15-debug/core-linux/runner/reboot_os_linux_runner_wine_gate

winhost=$(find "$payload" -name reboot-winhost.exe | head -n 1)
game=$(find "$payload" -name reboot-fake-game.exe | head -n 1)
if [ -z "$winhost" ] || [ -z "$game" ]; then
    echo "$payload holds no reboot-winhost.exe or reboot-fake-game.exe" >&2
    exit 1
fi

docker run --rm --init \
    -v "$wine:/opt/wine:ro" -v "$payload:/opt/payload:ro" \
    -e HOME=/tmp -e REBOOT_WINE_RUNTIME=/opt/wine \
    -e REBOOT_WINHOST_EXE="/opt/payload/${winhost#"$payload"/}" \
    -e REBOOT_FAKE_GAME_EXE="/opt/payload/${game#"$payload"/}" \
    "$image" "$tests" --reporter console --durations yes
