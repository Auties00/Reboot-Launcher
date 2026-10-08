#!/bin/sh
# Signs with the openssl CLI, not with trust, so a drift in the context or the .sig layout fails the test.
set -eu
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

openssl genpkey -algorithm ED25519 -out "$work/key.pem"
openssl pkey -in "$work/key.pem" -pubout -outform DER | tail -c 32 > "$work/pub.bin"
od -An -v -tx1 "$work/pub.bin" | tr -d ' \n' > known_answer.pub
key_id=$(openssl dgst -sha256 -binary "$work/pub.bin" | head -c 8 | od -An -v -tx1 | tr -d ' \n')

sign() {
    printf '%s\n' "$2" > "$work/message.bin"
    cat "$1" >> "$work/message.bin"
    openssl pkeyutl -sign -rawin -inkey "$work/key.pem" -in "$work/message.bin" -out "$work/signature.bin"
    printf 'ed25519 %s %s\n' "$key_id" "$(od -An -v -tx1 "$work/signature.bin" | tr -d ' \n')" > "$1.sig"
}

printf '{"schema":1,"serial":42}' > build_catalog.json
printf '{"schema":2,"serial":7}' > release_manifest.json
sign build_catalog.json reboot-launcher/build-catalog/v1
sign release_manifest.json reboot-launcher/release-manifest/v1
