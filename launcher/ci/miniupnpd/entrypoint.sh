#!/bin/sh
# Runs miniupnpd in the foreground: LAN on eth0 (the Docker bridge the tests share), WAN on a dummy
# interface. Needs --cap-add NET_ADMIN for the dummy link and the nftables rules mappings add.
set -eu

# A routable-looking address, since miniupnpd refuses to map behind a reserved external one.
ip link add wan0 type dummy
ip addr add 11.22.33.44/24 dev wan0
ip link set wan0 up

lan=$(ip -4 -o addr show dev eth0 | awk '{print $4}')
mkdir -p /run/miniupnpd
cat > /run/miniupnpd/test.conf <<EOF
ext_ifname=wan0
listening_ip=$lan
port=0
enable_upnp=yes
enable_natpmp=yes
secure_mode=no
system_uptime=yes
uuid=6f7b3a4e-1c2d-4e5f-8a9b-0c1d2e3f4a5b
allow 1024-65535 0.0.0.0/0 1024-65535
deny 0-65535 0.0.0.0/0 0-65535
EOF

# The table and chains miniupnpd adds its rules to, by its default names. Debian's nft_init.sh
# cannot do it: bookworm ships it without the miniupnpd_functions.sh it sources.
nft -f /dev/stdin <<'NFT'
table inet filter {
    chain forward {
        type filter hook forward priority 0; policy accept;
        jump miniupnpd
    }
    chain miniupnpd {
    }
    chain prerouting {
        type nat hook prerouting priority -100; policy accept;
        jump prerouting_miniupnpd
    }
    chain postrouting {
        type nat hook postrouting priority 100; policy accept;
        jump postrouting_miniupnpd
    }
    chain prerouting_miniupnpd {
    }
    chain postrouting_miniupnpd {
    }
}
NFT
exec miniupnpd -d -f /run/miniupnpd/test.conf
