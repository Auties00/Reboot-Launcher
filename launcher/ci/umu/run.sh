#!/bin/sh
# Runs core-linux/runner's [umu] gate case in the launcher build image: the Windows-built
# reboot-winhost.exe and reboot-fake-game.exe through umu-run, pressure-vessel and GE-Proton, with the
# Steam Linux Runtime umu downloads. Privileged, since pressure-vessel's bubblewrap makes namespaces.
# umu refuses root, so it runs as the runner's uid, with the host's passwd and group files for the
# lookups umu and pressure-vessel make. GE-Proton is mounted writable because Proton keeps a lock
# file in its own directory. --init reaps what Wine orphans.
#   run.sh <launcher build image> <extracted umu-launcher zipapp> <extracted GE-Proton> <directory holding both exes>
# With the case skipped the binary exits 4, so a missing input fails the job instead of passing it.
set -eu
image=$1
umu=$(cd "$2" && pwd)
proton=$(cd "$3" && pwd)
payload=$(cd "$4" && pwd)
tests=/src/launcher/build/linux-gcc15-debug/core-linux/runner/reboot_os_linux_runner_wine_gate

winhost=$(find "$payload" -name reboot-winhost.exe | head -n 1)
game=$(find "$payload" -name reboot-fake-game.exe | head -n 1)
if [ -z "$winhost" ] || [ -z "$game" ]; then
    echo "$payload holds no reboot-winhost.exe or reboot-fake-game.exe" >&2
    exit 1
fi

docker run --rm --init --privileged --user "$(id -u):$(id -g)"     -v /etc/passwd:/etc/passwd:ro -v /etc/group:/etc/group:ro \
    -v "$umu:/opt/umu:ro" -v "$proton:/opt/proton" -v "$payload:/opt/payload:ro" \
    -e HOME=/tmp -e REBOOT_UMU_LAUNCHER=/opt/umu -e REBOOT_GE_PROTON=/opt/proton \
    -e REBOOT_WINHOST_EXE="/opt/payload/${winhost#"$payload"/}" \
    -e REBOOT_FAKE_GAME_EXE="/opt/payload/${game#"$payload"/}" \
    "$image" "$tests" "[umu]" --reporter console --durations yes
