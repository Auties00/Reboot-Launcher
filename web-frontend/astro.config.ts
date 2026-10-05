import cloudflare from '@astrojs/cloudflare';
import { defineConfig } from 'astro/config';

export default defineConfig({
  site: 'https://rebootfn.org',
  adapter: cloudflare({
    // The default adds a Cloudflare Images binding; nothing here uses astro:assets.
    imageService: 'passthrough',
  }),
  // Otherwise the adapter provisions a KV namespace for sessions.
  session: false,
  build: {
    inlineStylesheets: 'always',
  },
  vite: {
    build: {
      // Lightning CSS (the default) breaks animation-timeline declarations and reorders properties.
      cssMinify: 'esbuild',
    },
  },
  devToolbar: { enabled: false },
});
