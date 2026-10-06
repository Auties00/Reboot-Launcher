#!/bin/sh
# Downloads the current DB-IP "IP to Country Lite" database (MaxMind MMDB format) into $1
# (default /var/lib/sb/geoip) and atomically replaces dbip-country-lite.mmdb.
# DB-IP publishes a new file each month; the previous month is used as a fallback early on the 1st.
# License: CC BY 4.0, attribution "IP Geolocation by DB-IP" (https://db-ip.com), see NOTICE.
set -eu

dir="${1:-/var/lib/sb/geoip}"
mkdir -p "$dir"
tmp="$(mktemp "$dir/.dbip.XXXXXX")"
trap 'rm -f "$tmp" "$tmp.gz"' EXIT

fetch() {
    curl -fsSL --retry 3 -o "$tmp.gz" "https://download.db-ip.com/free/dbip-country-lite-$1.mmdb.gz"
}

this_month="$(date -u +%Y-%m)"
last_month="$(date -u -d "$(date -u +%Y-%m-01) -1 day" +%Y-%m)"
if ! fetch "$this_month"; then
    fetch "$last_month"
fi

gunzip -c "$tmp.gz" > "$tmp"
# Sanity check: an MMDB file ends with its metadata section.
if ! tail -c 4096 "$tmp" | grep -q "MaxMind.com"; then
    echo "update-geoip: downloaded file is not an MMDB database" >&2
    exit 1
fi
chmod 0644 "$tmp"
mv -f "$tmp" "$dir/dbip-country-lite.mmdb"
cat > "$dir/NOTICE" <<'NOTICE_EOF'
IP Geolocation by DB-IP (https://db-ip.com), licensed under CC BY 4.0.
NOTICE_EOF
echo "update-geoip: installed $dir/dbip-country-lite.mmdb"
