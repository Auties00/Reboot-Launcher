# rebootfn.org

The Project Reboot landing page and its builds modal: an [Astro](https://astro.build) site deployed as a
Cloudflare Worker. Pages are prerendered; only `/api/availability` and the unknown-path redirect run in the Worker.

## Commands

| Command           | What it does                                          |
| ----------------- | ----------------------------------------------------- |
| `npm install`     | Install dependencies (Node 22.12+).                   |
| `npm run dev`     | Dev server at `localhost:4321`.                       |
| `npm run build`   | Type-check and build into `dist/`.                    |
| `npm run preview` | Build, then run the Worker locally with Wrangler.     |
| `npm run deploy`  | Build, then deploy with Wrangler.                     |

## Layout

```
src/
  pages/        index.astro, api/availability.ts, [...path].ts (unknown paths → /)
  layouts/      BaseLayout.astro
  components/   one component per section
  data/         site content; builds.json is the build catalogue the reels are generated from
  scripts/      client behaviour per component; builds/ is the modal, loaded on intent
  styles/       global.css imports the partials in cascade order; builds.css is the modal's
public/         static files and _headers
```

## Notes

- The hero's endless animations are stepped at 60 updates/s on a shared 1/60 s grid; keep new ones on it.
- CSS is minified with esbuild: Vite's default (Lightning CSS) breaks the `animation-timeline` declarations.
