import type { APIRoute } from 'astro';

export const prerender = false;

const UPSTREAM = 'https://builds.rebootfn.org/versions.json';

// Cached at the edge for five minutes; `cf` is Cloudflare's extension to RequestInit.
const EDGE_CACHE: RequestInit & { cf: { cacheTtl: number; cacheEverything: boolean } } = { cf: { cacheTtl: 300, cacheEverything: true } };

const fail = (reason: string) => new Response(reason, { status: 502 });

// Proxied, not fetched by the browser: the mirror shares no certificate with this origin, sends no-cache, and needs no CORS this way.
export const GET: APIRoute = async () => {
  let up: Response;
  try {
    up = await fetch(UPSTREAM, EDGE_CACHE);
  } catch {
    return fail('upstream unreachable');
  }
  if (!up.ok) return fail('upstream ' + up.status);

  let body: string;
  try {
    body = await up.text();
  } catch {
    return fail('upstream truncated');
  }
  let list: unknown;
  try {
    list = JSON.parse(body);
  } catch {
    return fail('upstream malformed');
  }
  // Never pass on an empty list: the client would read it as "nothing is available".
  if (!Array.isArray(list) || !list.length) return fail('upstream empty');

  return new Response(body, {
    headers: {
      'content-type': 'application/json',
      'cache-control': 'public, max-age=60, s-maxage=300, stale-while-revalidate=3600',
      'x-content-type-options': 'nosniff',
    },
  });
};
