import type { APIRoute } from 'astro';

export const prerender = false;

// Pages are prerendered and served before the Worker, so only unknown paths get here; missing assets stay 404s.
const ASSET_DIRS = /^\/(?:_astro|fonts|hero|seasons)\//;

export const ALL: APIRoute = ({ url, redirect }) =>
  ASSET_DIRS.test(url.pathname) ? new Response(null, { status: 404 }) : redirect('/', 302);
