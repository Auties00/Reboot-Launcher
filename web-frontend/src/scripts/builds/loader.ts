import cssUrl from '../../styles/builds.css?url';
import dataUrl from '../../data/builds.json?url';
import type { Catalogue } from '../../data/catalogue';

type ModalModule = typeof import('./modal');

interface Pending {
  code: Promise<ModalModule>;
  data: Promise<Catalogue>;
  live: Promise<unknown>;
  css: Promise<boolean>;
}

const OPENERS = '.film[data-season], a[href="#builds"]';
const HASH = /^#(?:builds|season-(\d+|pre))$/;

const stylesheet = () => new Promise<boolean>((done) => {
  const l = document.createElement('link');
  l.rel = 'stylesheet';
  l.href = cssUrl;
  l.onload = () => done(true);
  l.onerror = () => done(false);
  document.head.appendChild(l);
});

const askMirror = (): Promise<unknown> =>
  fetch('/api/availability', AbortSignal.timeout ? { signal: AbortSignal.timeout(4000) } : undefined)
    .then((r) => (r.ok ? r.json() : Promise.reject(new Error(String(r.status)))))
    .catch(() => null);

export function initBuildsLoader(): void {
  if (!window.HTMLDialogElement || !HTMLDialogElement.prototype.showModal) return;
  let pending: Pending | undefined;
  let mod: ModalModule | undefined;

  // ??= dedupes: repeated hovers make one set of requests.
  const warm = (): Pending => (pending ??= {
    code: import('./modal'),
    data: fetch(dataUrl).then((r) => (r.ok ? r.json() : Promise.reject(new Error(String(r.status))))),
    live: askMirror(),
    css: stylesheet(),
  });

  const retry = (): Promise<unknown> => (warm().live = askMirror());

  const open = (season: number | null, opener: HTMLElement | null) => {
    const p = warm();
    opener?.classList.add('is-loading');
    const done = () => opener?.classList.remove('is-loading');
    Promise.all([p.code, p.data, p.css]).then(([m, data, styled]) => {
      done();
      (mod = m).open({ data, live: p.live, season, opener, styled, retry });
    }, () => {
      done();
      pending = undefined;
    });
  };

  const hit = (e: Event) => (e.target instanceof Element ? e.target.closest<HTMLElement>(OPENERS) : null);
  const sure = (e: Event) => { if (hit(e)) warm(); };

  // Posters slide under a parked cursor while scrolling; the dwell filters those pointerovers out.
  let dwell = 0;
  const cancel = () => clearTimeout(dwell);
  document.addEventListener('pointerover', (e) => { cancel(); if (hit(e)) dwell = window.setTimeout(warm, 90); });
  document.addEventListener('pointerout', cancel);
  document.addEventListener('scroll', cancel, { capture: true, passive: true });
  document.addEventListener('focusin', sure);
  document.addEventListener('touchstart', sure, { passive: true });

  document.addEventListener('click', (e) => {
    // Reels cancel drag clicks in capture phase; modifier clicks open the season in a new tab.
    if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
    const a = hit(e);
    if (!a) return;
    e.preventDefault();
    open(a.dataset.season ? +a.dataset.season : null, a);
  });

  const fromHash = () => {
    const m = HASH.exec(location.hash);
    if (m) open(m[1] === 'pre' ? 0 : m[1] ? +m[1] : null, null);
  };
  fromHash();
  addEventListener('hashchange', () => { if (!mod?.isOpen()) fromHash(); });
}
