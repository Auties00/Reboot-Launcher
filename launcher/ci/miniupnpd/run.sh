#!/bin/sh
# Runs core/net's [integration] gateway tests against miniupnpd, both on one Docker bridge with the
# tests' default route through miniupnpd, since NAT-PMP asks the default gateway.
#   run.sh <launcher build image> <miniupnpd image>
# The build image is linux.Dockerfile's "build" stage.
set -eu
image=$1
daemon_image=$2
network=reboot-gateway
daemon_ip=172.31.7.2
tests=/src/launcher/build/linux-gcc15-debug/core/net/reboot_net_tests

cleanup() {
    docker rm -f reboot-miniupnpd reboot-gateway-tests >/dev/null 2>&1 || true
    docker network rm "$network" >/dev/null 2>&1 || true
}
trap cleanup EXIT
cleanup

docker network create --subnet 172.31.7.0/24 "$network"
docker run -d --name reboot-miniupnpd --network "$network" --ip "$daemon_ip" --cap-add NET_ADMIN "$daemon_image"
docker run -d --name reboot-gateway-tests --network "$network" --ip 172.31.7.3 -e REBOOT_MINIUPNPD="$daemon_ip" \
    --entrypoint sleep "$image" infinity
docker run --rm --network container:reboot-gateway-tests --cap-add NET_ADMIN alpine:3.20 \
    ip route replace default via "$daemon_ip"

ready=0
for _ in $(seq 1 30); do
    if docker logs reboot-miniupnpd 2>&1 | grep -q "HTTP listening"; then ready=1; break; fi
    sleep 1
done
if [ "$ready" != 1 ]; then
    docker logs reboot-miniupnpd
    echo "miniupnpd did not start" >&2
    exit 1
fi

status=0
docker exec reboot-gateway-tests "$tests" "[integration]" --reporter console || status=$?
docker logs reboot-miniupnpd 2>&1 | tail -n 50
exit "$status"
