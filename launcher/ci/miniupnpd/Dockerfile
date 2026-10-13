# miniupnpd with UPnP IGD and NAT-PMP, for core/net's [integration] gateway tests (run.sh).
#   docker build -t reboot-miniupnpd launcher/ci/miniupnpd
FROM debian:bookworm-slim
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends miniupnpd-nftables nftables iproute2 \
    && rm -rf /var/lib/apt/lists/*
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
