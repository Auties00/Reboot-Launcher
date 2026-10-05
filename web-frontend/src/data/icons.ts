interface IconDef {
  paint: 'fill' | number;
  body: string;
}

export const ICONS = {
  discord: {
    paint: 'fill',
    body: '<path d="M19.6 5.6A16 16 0 0 0 15.7 4.4l-.5 1a15 15 0 0 0-6.4 0l-.5-1a16 16 0 0 0-3.9 1.2C2 9.3 1.3 12.9 1.6 16.5a16 16 0 0 0 4.8 2.4l1-1.6a10 10 0 0 1-1.6-.8l.4-.3a11.5 11.5 0 0 0 11.6 0l.4.3a10 10 0 0 1-1.6.8l1 1.6a16 16 0 0 0 4.8-2.4c.4-4.1-.7-7.7-2.8-10.9ZM8.7 14.3c-.9 0-1.7-.9-1.7-2s.7-2 1.7-2 1.7.9 1.7 2-.8 2-1.7 2Zm6.6 0c-.9 0-1.7-.9-1.7-2s.7-2 1.7-2 1.7.9 1.7 2-.8 2-1.7 2Z"/>',
  },
  github: {
    paint: 'fill',
    body: '<path d="M12 .5a12 12 0 0 0-3.8 23.4c.6.1.8-.3.8-.6v-2.2c-3.3.7-4-1.4-4-1.4-.5-1.4-1.3-1.8-1.3-1.8-1.1-.7.1-.7.1-.7 1.2.1 1.8 1.2 1.8 1.2 1.1 1.9 2.8 1.3 3.5 1 .1-.8.4-1.3.8-1.6-2.7-.3-5.5-1.3-5.5-5.9 0-1.3.5-2.4 1.2-3.2-.1-.3-.5-1.5.1-3.2 0 0 1-.3 3.3 1.2a11.5 11.5 0 0 1 6 0c2.3-1.5 3.3-1.2 3.3-1.2.7 1.7.3 2.9.1 3.2.8.8 1.2 1.9 1.2 3.2 0 4.6-2.8 5.6-5.5 5.9.4.4.8 1.1.8 2.2v3.3c0 .3.2.7.8.6A12 12 0 0 0 12 .5Z"/>',
  },
  download: {
    paint: 2.2,
    body: '<path d="M12 3v12"/><path d="m8 11 4 4 4-4"/><path d="M4 16v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
  },
  prev: { paint: 2.4, body: '<path d="m15 5-7 7 7 7"/>' },
  next: { paint: 2.4, body: '<path d="m9 5 7 7-7 7"/>' },
  monitor: { paint: 2, body: '<rect x="3" y="4" width="18" height="14" rx="2"/><path d="M8 21h8M12 18v3"/>' },
  crosshair: { paint: 2, body: '<path d="M12 3v3M12 18v3M3 12h3M18 12h3"/><circle cx="12" cy="12" r="6"/>' },
  list: { paint: 2, body: '<path d="M4 7h16M4 12h10M4 17h7"/>' },
  terminal: { paint: 2, body: '<path d="M4 5h16v14H4z"/><path d="m8 10 2 2-2 2M12 14h4"/>' },
  shield: { paint: 2, body: '<path d="M12 3l8 4v5c0 5-3.5 8-8 9-4.5-1-8-4-8-9V7l8-4z"/><path d="m9 12 2 2 4-4"/>' },
  prompt: { paint: 2, body: '<path d="M4 17l6-5-6-5M12 19h8"/>' },
} satisfies Record<string, IconDef>;

export type IconName = keyof typeof ICONS;
