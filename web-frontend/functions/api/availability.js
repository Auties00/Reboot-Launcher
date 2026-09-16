// What is downloadable is only ever asserted by the bucket itself. This proxies the
// bucket's own index rather than letting the browser fetch it directly, for three
// measured reasons: rebootfn.org and builds.rebootfn.org resolve to the same IPs but
// their certificates are disjoint (rebootfn.org has no wildcard SAN), so H2 connection
// coalescing is impossible and a direct fetch pays a ~250ms DNS+TCP+TLS handshake for a
// 489-byte body; R2 serves the object cache-control: no-cache / cf-cache-status: DYNAMIC,
// so nothing caches it anywhere; and a server-to-server fetch cannot be broken by a lost
// R2 CORS policy the way a browser fetch silently can.
//
// It answers with a non-2xx on any upstream trouble and never with an empty array: an
// empty array would read to the client as "nothing is available", which is the single
// most dangerous lie this feature can tell.
export async function onRequestGet() {
  let up;
  try {
    up = await fetch('https://builds.rebootfn.org/versions.json', {
      cf: { cacheTtl: 300, cacheEverything: true },
    });
  } catch {
    return new Response('upstream unreachable', { status: 502 });
  }
  if (!up.ok) return new Response('upstream ' + up.status, { status: 502 });

  // Buffering the body is the only way to keep the promise made above, and on 7 KB it costs
  // nothing. builds.js checks the same thing when it applies the result, on the principle
  // that the client should not have to trust a network hop, but a guard that lives only in
  // the caller is one refactor away from being deleted as redundant, so it lives here too.
  let body;
  try {
    body = await up.text();
  } catch {
    return new Response('upstream truncated', { status: 502 });
  }
  let list;
  try {
    list = JSON.parse(body);
  } catch {
    return new Response('upstream malformed', { status: 502 });
  }
  if (!Array.isArray(list) || !list.length) return new Response('upstream empty', { status: 502 });

  return new Response(body, {
    headers: {
      'content-type': 'application/json',
      'cache-control': 'public, max-age=60, s-maxage=300, stale-while-revalidate=3600',
      'x-content-type-options': 'nosniff',
    },
  });
}
