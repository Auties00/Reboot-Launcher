// Unknown paths go back to the landing page.
//
// This deliberately is not a _redirects splat: Cloudflare Pages evaluates _redirects
// *before* it looks for a static asset, so "/*  /  302" sends "/" to itself and
// redirects every font and image too. _routes.json below lists the real assets as
// exclusions, so this middleware only ever runs for a path that has nothing behind it.
//
// It still calls next() first, so a file added later that nobody remembered to add to
// _routes.json is served normally instead of being bounced to the home page.
export async function onRequest(context) {
  const response = await context.next();
  if (response.status !== 404) return response;
  return Response.redirect(new URL('/', context.request.url).toString(), 302);
}
