# Reboot Launcher

Greenfield rewrite. This branch starts from an empty tree and grows one component at a time.

- `web-frontend/`: the rebootfn.org site (Astro + TypeScript), the landing page and the builds modal, deployed to Cloudflare Pages.
- `server-browser/`: the real-time server browser service (C++26 + MsQuic), clustered over NATS JetStream, replacing the old WebSocket backend.
