import type { Catalogue } from '../../data/catalogue';
import { Availability } from './availability';
import { art, Catalog } from './catalog';
import { PullStretch } from './pull';
import { MIRROR, Sizes } from './sizes';
import { downloadTitle, SHELL, Templates } from './templates';
import { RailThumbs } from './thumbs';
import { glide, isPhone, onPhoneChange, reduceMotion, spokenSize } from './util';
import type { Platform } from './platforms';

export interface OpenContext {
  data: Catalogue;
  live: Promise<unknown> | null;
  season: number | null;
  opener: HTMLElement | null;
  styled: boolean;
  retry: () => Promise<unknown>;
}

// Used when builds.css fails to load.
const FALLBACK = '.bl{width:min(1220px,94vw);height:86vh;padding:0;color:var(--ink);background:var(--panel);border:1px solid var(--line-strong);display:grid;grid-template-columns:280px minmax(0,1fr);overflow:hidden}.bl-side{display:flex;flex-direction:column;min-height:0;border-right:1px solid var(--line)}.bl-side-head{padding:12px 16px}.bl-side-t{margin:0;font-size:20px}.bl-list{flex:1;overflow-y:auto}.bl-srow{display:block;width:100%;text-align:left;padding:6px 12px;color:inherit;background:none;border:0;font:inherit}.bl-th img{display:none}.bl-canvas{display:flex;flex-direction:column;min-height:0;position:relative}.bl-scroll{flex:1;overflow-y:auto;min-height:0}.bl-hero{display:none}.bl-topbar{display:flex;justify-content:space-between;padding:12px 16px}.bl-detail{padding:0 24px 32px}.bl-title{font-size:32px;margin:6px 0 12px}.bl-grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fill,minmax(260px,1fr))}.bl-card{padding:12px;border:1px solid var(--line);border-radius:6px}.bl-go{display:inline-block;padding:6px 12px;background:var(--storm);color:#04121C;border-radius:6px;text-decoration:none;font-weight:700}.bl-vh,.bl-say{position:absolute;clip-path:inset(50%)}html.bl-lock{overflow:hidden;padding-right:var(--bl-sbw,0px)}.bl:not([open]){display:none!important}.bl-close-side{display:none}@media(max-width:760px){.bl{grid-template-columns:1fr}.bl-canvas{display:none}.bl.is-pushed .bl-side{display:none}.bl.is-pushed .bl-canvas{display:block}.bl-close-side{display:inline-block}}';

const OWN_HASH = /^#(?:builds|season-(?:\d+|pre))$/;
const hashFor = (n: number) => (n === 0 ? '#season-pre' : '#season-' + n);

const CASCADE = 900;
const FOCUSABLE = 'a[href],button,input,select,textarea,[tabindex]';

type FocusMemo = { title: true } | { k: string } | { pick: string } | { row: string } | { card: string } | null;

class BuildsModal {
  private readonly cat: Catalog;
  private readonly live: Availability;
  private readonly sizes: Sizes;
  private readonly tpl: Templates;

  private readonly dlg: HTMLDialogElement;
  private readonly side: HTMLElement;
  private readonly list: HTMLElement;
  private readonly canvas: HTMLElement;
  private readonly head: HTMLElement;
  private readonly headin: HTMLElement;
  private readonly scroller: HTMLElement;
  private readonly hero: HTMLElement;
  private readonly detail: HTMLElement;
  private readonly sayEl: HTMLElement;
  private readonly thumbs: RailThumbs;
  private banner: HTMLElement | null = null;

  private season: number | null = null;
  private heroN: number | null = null;
  private opener: HTMLElement | null = null;
  private retryFn: (() => Promise<unknown>) | null = null;
  private railHTML = '';
  private cascadeStart = new WeakMap<HTMLElement, number>();

  private menu: HTMLElement | null = null;
  private menuCard: HTMLElement | null = null;

  private pushed = false;
  private popping = false;
  private viewPushed = false;
  private depth = 0;
  private suppress = false;
  private shieldT = 0;
  private stripOwed = false;
  private place: [number, number] | null = null;
  private restore: ScrollRestoration = 'auto';
  private fromId = '';
  private hid: Element | null = null;
  private warmed = false;

  constructor(data: Catalogue, styled: boolean) {
    this.cat = new Catalog(data);
    this.live = new Availability(this.cat);
    this.sizes = new Sizes(this.cat, this.live, (k, text) => this.showSize(k, text));
    this.tpl = new Templates(this.cat, this.live, this.sizes);

    if (!styled) {
      const fb = document.createElement('style');
      fb.textContent = FALLBACK;
      document.head.appendChild(fb);
    }

    const dlg = this.dlg = document.createElement('dialog');
    dlg.id = 'bl-dialog';
    dlg.className = 'bl';
    dlg.setAttribute('aria-label', 'Builds');
    dlg.innerHTML = SHELL;
    document.body.appendChild(dlg);

    const $ = (sel: string) => dlg.querySelector<HTMLElement>(sel)!;
    this.side = $('.bl-side');
    this.list = $('.bl-list');
    this.canvas = $('.bl-canvas');
    this.hero = $('.bl-hero');
    this.detail = $('.bl-detail');
    this.head = $('.bl-head');
    this.headin = $('.bl-headin');
    this.scroller = $('.bl-scroll');
    this.sayEl = $('.bl-say');
    this.thumbs = new RailThumbs(this.list, this.side);
    new PullStretch(this.scroller, this.head, this.hero);

    dlg.addEventListener('click', (e) => this.onClick(e));
    dlg.addEventListener('keydown', (e) => this.onKey(e));
    this.list.addEventListener('keydown', (e) => this.onListKey(e));
    dlg.addEventListener('cancel', (e) => { e.preventDefault(); this.closeModal(false); });
    dlg.addEventListener('close', () => this.onClose());
    addEventListener('popstate', () => this.onPop());
    addEventListener('hashchange', () => {
      if (this.suppress) { clearTimeout(this.shieldT); this.suppress = false; }
      this.settle();
    });
    onPhoneChange(() => this.syncStack());
  }

  isOpen(): boolean {
    return this.dlg.open || this.suppress;
  }

  open(ctx: OpenContext): void {
    this.retryFn = ctx.retry;
    const want = ctx.season != null && this.cat.has(ctx.season) ? ctx.season : this.cat.firstSeason();

    if (this.dlg.open) {
      this.selectSeason(want);
      if (isPhone() && ctx.season != null) this.pushView();
      return;
    }
    this.opener = ctx.opener;
    clearTimeout(this.shieldT);
    this.suppress = false;

    // Back must close the modal, so open pushes an entry; restoration goes manual so the close cannot re-scroll to a page #fragment.
    this.place = [scrollX, scrollY];
    this.fromId = OWN_HASH.test(location.hash) ? '' : location.hash.slice(1);
    this.restore = history.scrollRestoration;
    try { history.scrollRestoration = 'manual'; } catch {}
    history.pushState({ bl: 1, d: 0 }, '', hashFor(want));
    this.pushed = true;
    this.depth = 0;
    this.viewPushed = isPhone() && ctx.season != null;

    // Before renderDetail's HEAD probes, or it warms nothing.
    this.preconnect();
    this.lock();
    const dlg = this.dlg;
    dlg.classList.toggle('is-pushed', this.viewPushed);
    dlg.classList.add('is-opening');
    const o = this.opener?.getBoundingClientRect();
    // Grow from the tapped poster.
    dlg.style.transformOrigin = o ? `calc(${o.x + o.width / 2}px - 50vw + 50%) calc(${o.y + o.height / 2}px - 50dvh + 50%)` : '';
    dlg.showModal();
    this.season = want;
    this.renderSide();
    this.currentRow()?.scrollIntoView({ block: 'nearest' });
    this.renderDetail(want, true);
    this.syncStack();
    dlg.setAttribute('aria-labelledby', 'bl-title');
    this.focusIn(ctx.season != null);

    if (this.live.state === 'loading' && ctx.live) this.await(ctx.live);
    else if (this.live.state === 'failed') this.retryLive();
  }

  private preconnect(): void {
    if (this.warmed) return;
    this.warmed = true;
    const l = document.createElement('link');
    l.rel = 'preconnect';
    l.href = MIRROR;
    l.crossOrigin = '';
    document.head.appendChild(l);
  }

  private focusIn(seasonAsked: boolean): void {
    const desktop = matchMedia('(hover: hover) and (pointer: fine)').matches;
    const title = this.headin.querySelector<HTMLElement>('#bl-title'), row = this.currentRow();
    let t: HTMLElement | null;
    if (isPhone() && !this.viewPushed) t = row ?? title;
    else if (seasonAsked) t = title ?? row;
    else t = desktop ? row ?? title : title ?? row;
    (t ?? this.dlg).focus({ preventScroll: true });
  }

  private lock(): void {
    const sbw = innerWidth - document.documentElement.clientWidth;
    document.documentElement.style.setProperty('--bl-sbw', sbw + 'px');
    document.documentElement.classList.add('bl-lock');
  }

  private unlock(): void {
    document.documentElement.classList.remove('bl-lock');
    document.documentElement.style.removeProperty('--bl-sbw');
  }

  private await(p: Promise<unknown>): void {
    p.then((v) => this.applyLive(v), () => this.applyLive(null));
  }

  private applyLive(urls: unknown): void {
    this.live.apply(urls);
    if (this.live.state === 'failed') this.showBanner(); else this.hideBanner();
    this.detail.classList.add('is-answer');
    this.rebuild();
    setTimeout(() => this.detail.classList.remove('is-answer'), 600);
    this.say(this.live.state === 'ok' ? 'Availability confirmed.'
      : 'Availability could not be checked. Builds are not shown until it can be.');
  }

  private retryLive(): void {
    if (!this.retryFn || this.live.state === 'loading') return;
    const act = document.activeElement;
    // hideBanner removes the focused button, so focus is put back on the title after the rebuild.
    const hadRetry = !!act?.classList.contains('bl-retry');
    this.live.reset();
    this.hideBanner();
    this.rebuild();
    if (hadRetry) this.headin.querySelector<HTMLElement>('#bl-title')?.focus({ preventScroll: true });
    this.say('Checking the mirror again.');
    this.await(this.retryFn());
  }

  private say(msg: string): void {
    if (this.sayEl.textContent !== msg) this.sayEl.textContent = msg;
  }

  private showBanner(): void {
    if (this.banner) return;
    const b = this.banner = document.createElement('div');
    b.className = 'bl-banner';
    b.innerHTML = '<span>Couldn’t reach the mirror, so the builds can’t be shown right now.</span>' +
      '<button class="btn btn-ghost bl-retry" type="button"><span>Try again</span></button>';
    this.dlg.classList.add('has-banner');
    this.placeBanner();
  }

  private placeBanner(): void {
    const b = this.banner;
    if (!b) return;
    if (!(isPhone() && !this.viewPushed)) { b.remove(); return; }
    if (b.parentNode !== this.side) this.side.insertBefore(b, this.list);
  }

  private hideBanner(): void {
    if (!this.banner) return;
    this.banner.remove();
    this.banner = null;
    this.dlg.classList.remove('has-banner');
  }

  private showSize(k: string, text: string): void {
    const a = this.detail.querySelector<HTMLElement>(`[data-k="${CSS.escape(k)}"]`);
    if (!a) return;
    a.title = downloadTitle(text);
    const label = a.getAttribute('aria-label') ?? '';
    a.setAttribute('aria-label', label.replace(/, [\d.]+ (gigabytes|megabytes)/, '').replace(/\.$/, '') + spokenSize(text) + '.');
  }

  private renderSide(): void {
    this.paintRail(this.tpl.rail(this.season));
    this.cascade(this.list, this.list.dataset.view !== 'all', [...this.list.children] as HTMLElement[]);
    this.list.dataset.view = 'all';
  }

  private paintRail(h: string): void {
    // Identical markup: re-parsing would only drop the hydrated thumbnails.
    if (h === this.railHTML) return;
    this.railHTML = h;
    this.list.innerHTML = h;
    this.thumbs.watch();
  }

  private renderDetail(n: number, fresh: boolean): void {
    const top = this.scroller.scrollTop;
    this.closeMenu();
    this.setHero(n);
    this.headin.innerHTML = this.tpl.head(n);
    this.detail.innerHTML = this.tpl.body(n);
    this.cascade(this.headin, fresh);
    this.cascade(this.detail, fresh);
    this.scroller.scrollTop = fresh ? 0 : top;
    try { this.sizes.probe(this.cat.bySeason.get(n)!); } catch {}
  }

  private rebuild(): void {
    const memo = this.focusMemo(), top = this.list.scrollTop;
    this.renderSide();
    this.list.scrollTop = top;
    if (this.season != null) this.renderDetail(this.season, false);
    this.focusBack(memo);
  }

  private setHero(n: number): void {
    if (this.heroN === n) { this.landArt(); return; }
    this.heroN = n;
    this.hero.classList.remove('is-in', 'no-art');
    this.hero.innerHTML = `<img class="bl-wash" src="${art(n, '-blur')}" alt="" width="64" height="36" decoding="async">`;
    this.holdPush(this.hero.firstElementChild as HTMLElement);
    this.landArt();
  }

  private landArt(): void {
    const n = this.heroN;
    // The 640px art waits until the canvas is on stage.
    if (n == null || this.hero.querySelector('.bl-art') || (isPhone() && !this.viewPushed)) return;
    this.hero.insertAdjacentHTML('beforeend', `<img class="bl-art" src="${art(n)}" srcset="${art(n, '-480')} 480w, ${art(n)} 640w" sizes="(max-width: 760px) 100vw, 924px" alt="" width="640" height="360" decoding="async">`);
    const img = this.hero.lastElementChild as HTMLImageElement;
    const land = () => {
      if (this.heroN !== n) return;
      this.holdPush(img);
      this.hero.classList.add('is-in');
    };
    if (img.complete) land(); else img.onload = land;
  }

  // Chrome leaks a 1px column of the art past the hero's clip while its push-in and the dialog's
  // scale-in run together, so the push waits out whatever is left of the entrance.
  private holdPush(img: HTMLElement): void {
    const entrance = this.dlg.getAnimations().find((a) => (a as CSSAnimation).animationName === 'bl-in');
    const end = Number(entrance?.effect?.getComputedTiming().endTime ?? 0);
    const left = entrance && entrance.playState !== 'finished' ? end - Number(entrance.currentTime ?? 0) : 0;
    img.style.animationDelay = left > 0 ? `0s, ${Math.ceil(left)}ms` : '';
  }

  // Starts or resumes the entrance cascade: a rebuild mid-cascade continues it via a negative --el.
  private cascade(el: HTMLElement, fresh: boolean, items?: HTMLElement[]): void {
    const now = performance.now();
    const off = isPhone() && (el === this.list ? this.viewPushed : !this.viewPushed);
    if (fresh) this.cascadeStart.set(el, now);
    const dt = now - (this.cascadeStart.get(el) ?? -CASCADE);
    if (off || (!fresh && dt > CASCADE)) { el.classList.remove('is-fresh'); return; }
    el.classList.add('is-fresh');
    el.style.setProperty('--el', (fresh ? 0 : -Math.round(dt)) + 'ms');
    if (!items) return;
    const c = items.findIndex((k) => k.getAttribute('aria-current') === 'true'), from = Math.max(0, c - 10);
    for (let i = from; i < Math.min(items.length, from + 14); i++) items[i].style.setProperty('--i', String(i - from));
  }

  private currentRow(): HTMLElement | null {
    return this.list.querySelector('.bl-srow[aria-current="true"]');
  }

  private markCurrent(n: number): void {
    for (const r of this.list.querySelectorAll<HTMLElement>('.bl-srow')) {
      const on = +r.dataset.season! === n;
      r.setAttribute('aria-current', on ? 'true' : 'false');
      r.tabIndex = on ? 0 : -1;
      if (on) r.scrollIntoView({ block: 'nearest', behavior: glide() });
    }
  }

  private focusMemo(): FocusMemo {
    const el = document.activeElement as HTMLElement | null;
    if (!el || !this.dlg.contains(el)) return null;
    if (el.id === 'bl-title') return { title: true };
    if (el.dataset.k) return { k: el.dataset.k };
    if (el.classList.contains('bl-sel')) return { pick: el.closest<HTMLElement>('.bl-card')!.dataset.v! };
    if (el.classList.contains('bl-srow')) return { row: el.dataset.season! };
    if (el.dataset.v) return { card: el.dataset.v };
    return null;
  }

  private focusBack(m: FocusMemo): void {
    if (!m) return;
    let t: HTMLElement | null = null;
    if ('title' in m) t = this.headin.querySelector('#bl-title');
    else if ('k' in m) t = this.detail.querySelector(`[data-k="${CSS.escape(m.k)}"]`);
    else if ('card' in m) t = this.detail.querySelector(`[data-v="${CSS.escape(m.card)}"]`);
    else if ('pick' in m) t = this.detail.querySelector(`[data-v="${CSS.escape(m.pick)}"] .bl-sel`);
    else if ('row' in m) t = this.list.querySelector(`.bl-srow[data-season="${m.row}"]`);
    t?.focus({ preventScroll: true });
  }

  private selectSeason(n: number): void {
    const fresh = n !== this.season;
    if (fresh && this.season != null) this.dlg.classList.remove('is-opening');
    this.season = n;
    this.markCurrent(n);
    if (fresh) this.renderDetail(n, true);
    // replaceState: Back should leave the modal, not walk the seasons.
    history.replaceState({ bl: 1, d: this.depth }, '', hashFor(n));
  }

  private syncStack(): void {
    const phone = isPhone();
    this.dlg.classList.toggle('is-pushed', this.viewPushed);
    this.canvas.inert = phone && !this.viewPushed;
    this.side.inert = phone && this.viewPushed;
    this.placeBanner();
    this.thumbs.watch();
    this.landArt();
  }

  private pushView(): void {
    if (this.viewPushed) return;
    this.viewPushed = true;
    this.dlg.classList.remove('is-opening');
    this.syncStack();
    if (!this.detail.classList.contains('is-fresh')) { this.cascade(this.headin, true); this.cascade(this.detail, true); }
    // The phone's page owns an entry, so Back pops it before closing the dialog.
    if (!this.depth && this.season != null) { history.pushState({ bl: 1, d: 1 }, '', hashFor(this.season)); this.depth = 1; }
    this.headin.querySelector<HTMLElement>('#bl-title')?.focus({ preventScroll: true });
  }

  private popView(): void {
    if (!this.viewPushed) return;
    this.viewPushed = false;
    this.syncStack();
    this.currentRow()?.focus({ preventScroll: true });
  }

  private goBack(): void {
    if (this.depth) history.back();
    else this.popView();
  }

  private closeModal(fromPop: boolean): void {
    if (!this.dlg.open) return;
    this.popping ||= fromPop;
    if (this.dlg.classList.contains('is-closing')) return;
    if (reduceMotion()) { this.dlg.close(); return; }
    // The exit plays before dlg.close().
    this.dlg.classList.add('is-closing');
    this.closeMenu();
    setTimeout(() => { if (this.dlg.open) this.dlg.close(); }, 340);
  }

  private onClose(): void {
    this.unlock();
    this.dlg.classList.remove('is-closing', 'is-opening');
    delete this.list.dataset.view;
    this.railHTML = '';
    this.heroN = null;
    this.hero.className = 'bl-hero no-art';
    this.hero.innerHTML = '';
    this.closeMenu();
    const owed = this.pushed && !this.popping;
    const steps = this.depth + 1;
    this.pushed = false;
    this.popping = false;
    this.depth = 0;
    this.viewPushed = false;
    if (owed) {
      let el: Element | null = null;
      try { el = this.fromId ? document.getElementById(decodeURIComponent(this.fromId)) : null; } catch {}
      // Hide the fragment's target so the traversal cannot scroll to it; onPop restores the id.
      if (el) { this.hid = el; el.removeAttribute('id'); }
      this.suppress = true;
      this.stripOwed = true;
      history.go(-steps);
    } else {
      this.stripHash();
      try { history.scrollRestoration = this.restore; } catch {}
      this.place = null;
    }
    const o = this.opener;
    if (!o) return;
    this.opener = null;
    // preventScroll: focusing a .film would scroll its reel sideways.
    o.focus({ preventScroll: true });
    const r = o.getBoundingClientRect();
    if (r.bottom < 0 || r.top > innerHeight || r.right < 0 || r.left > innerWidth) {
      o.scrollIntoView({ block: 'nearest', inline: 'center', behavior: glide() });
    }
  }

  private onPop(): void {
    if (!this.dlg.open) {
      if (this.suppress) this.shield();
      if (this.stripOwed) {
        this.stripOwed = false;
        this.stripHash();
        try { history.scrollRestoration = this.restore; } catch {}
        this.settle();
        requestAnimationFrame(() => requestAnimationFrame(() => {
          if (this.hid) {
            try { this.hid.id = decodeURIComponent(this.fromId); } catch { this.hid.id = this.fromId; }
            this.hid = null;
          }
          this.settle();
          this.place = null;
        }));
      }
      return;
    }
    if (this.depth) { this.depth = 0; this.popView(); return; }
    this.shield();
    this.closeModal(true);
  }

  // The traversal's hashchange trails its popstate; stay "open" until it passes, or the hash reopens the modal.
  private shield(): void {
    this.suppress = true;
    clearTimeout(this.shieldT);
    this.shieldT = window.setTimeout(() => { this.suppress = false; }, 250);
  }

  private settle(): void {
    if (this.place) scrollTo({ left: this.place[0], top: this.place[1], behavior: 'instant' });
  }

  private stripHash(): void {
    if (OWN_HASH.test(location.hash)) history.replaceState(history.state, '', location.pathname + location.search);
  }

  private selectPlat(card: HTMLElement, p: Platform): void {
    const g = this.cat.group(card.dataset.v!);
    if (!g || !g.x.some((x) => x.p === p)) return;
    card.dataset.sel = p;
    card.querySelector('.bl-act')!.outerHTML = this.tpl.action(g, p);
    card.querySelector('.bl-meta')!.innerHTML = this.tpl.meta(g, p);
  }

  private openMenu(card: HTMLElement): void {
    if (this.menuCard === card) { this.closeMenu(true); return; }
    this.closeMenu();
    const g = this.cat.group(card.dataset.v!);
    if (!g) return;
    const menu = this.menu = document.createElement('div');
    menu.className = 'bl-menu';
    menu.setAttribute('role', 'menu');
    menu.setAttribute('aria-label', 'Platform');
    menu.innerHTML = this.tpl.menu(g, card.dataset.sel!);
    const holder = card.querySelector<HTMLElement>('.bl-act')!, pick = holder.querySelector('button.bl-sel')!;
    holder.appendChild(menu);
    this.menuCard = card;
    pick.setAttribute('aria-expanded', 'true');
    const r = holder.getBoundingClientRect(), sr = this.scroller.getBoundingClientRect(), h = menu.offsetHeight + 8;
    menu.classList.toggle('is-up', sr.bottom - r.bottom < h && r.top - sr.top > h);
    const cur = (menu.querySelector<HTMLElement>('[aria-checked="true"]') ?? menu.firstElementChild) as HTMLElement;
    cur.tabIndex = 0;
    cur.focus({ preventScroll: true });
  }

  private closeMenu(refocus = false): void {
    const m = this.menu, card = this.menuCard;
    if (!m || !card) return;
    this.menu = null;
    this.menuCard = null;
    if (reduceMotion()) m.remove();
    else { m.classList.add('is-out'); setTimeout(() => m.remove(), 200); }
    const pick = card.querySelector<HTMLElement>('button.bl-sel');
    if (pick) {
      pick.setAttribute('aria-expanded', 'false');
      if (refocus) pick.focus({ preventScroll: true });
    }
  }

  private onMenuKey(e: KeyboardEvent): boolean {
    const menu = this.menu;
    if (!menu) return false;
    const target = e.target as Node;
    const onPick = target === this.menuCard?.querySelector('button.bl-sel');
    if (!menu.contains(target) && !onPick) return false;
    if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); this.closeMenu(true); return true; }
    if (e.key === 'Tab') { this.closeMenu(); return true; }
    const all = [...menu.querySelectorAll<HTMLElement>('.bl-mi')];
    let k = all.indexOf(document.activeElement as HTMLElement);
    if (e.key === 'ArrowDown') k = (k + 1) % all.length;
    else if (e.key === 'ArrowUp') k = (k - 1 + all.length) % all.length;
    else if (e.key === 'Home') k = 0;
    else if (e.key === 'End') k = all.length - 1;
    else return false;
    e.preventDefault();
    for (const it of all) it.tabIndex = -1;
    all[k].tabIndex = 0;
    all[k].focus({ preventScroll: true });
    return true;
  }

  private onClick(e: MouseEvent): void {
    const t = e.target instanceof Element ? e.target : null;
    if (this.menu && !t?.closest('.bl-menu,button.bl-sel')) this.closeMenu();
    if (e.target === this.dlg) { this.closeModal(false); return; }
    if (!t) return;
    const pick = t.closest('button.bl-sel');
    if (pick) { this.openMenu(pick.closest<HTMLElement>('.bl-card')!); return; }
    const mi = t.closest<HTMLElement>('.bl-mi');
    if (mi) {
      const card = mi.closest<HTMLElement>('.bl-card')!, link = mi.tagName === 'A';
      this.closeMenu();
      this.selectPlat(card, mi.dataset.plat as Platform);
      card.querySelector<HTMLElement>('button.bl-sel')?.focus({ preventScroll: true });
      if (link) sent(card.querySelector('.bl-go'));
      return;
    }
    sent(t.closest('a.bl-go'));
    const el = t.closest<HTMLElement>('.bl-srow,.bl-close,.bl-back,.bl-retry');
    if (!el) return;
    if (el.classList.contains('bl-close')) return this.closeModal(false);
    if (el.classList.contains('bl-back')) return this.goBack();
    if (el.classList.contains('bl-retry')) return this.retryLive();
    this.selectSeason(+el.dataset.season!);
    if (isPhone()) this.pushView();
  }

  private onKey(e: KeyboardEvent): void {
    if (this.onMenuKey(e)) return;
    if (e.key === 'Escape') {
      // The UA close would skip the exit animation.
      e.preventDefault();
      this.closeModal(false);
      return;
    }
    if (e.key !== 'Tab') return;
    const f = this.focusables();
    if (!f.length) { e.preventDefault(); return; }
    const first = f[0], last = f[f.length - 1], at = document.activeElement;
    if (e.shiftKey && (at === first || at === this.dlg)) { e.preventDefault(); last.focus(); }
    else if (!e.shiftKey && at === last) { e.preventDefault(); first.focus(); }
  }

  private focusables(): HTMLElement[] {
    const out: HTMLElement[] = [];
    for (const el of this.dlg.querySelectorAll<HTMLElement>(FOCUSABLE)) {
      if (el.tabIndex >= 0 && !el.hidden && el.getClientRects().length && !el.closest('[inert]')) out.push(el);
    }
    return out;
  }

  private onListKey(e: KeyboardEvent): void {
    const items = [...this.list.querySelectorAll<HTMLElement>('[data-nav]')];
    const i = items.indexOf(document.activeElement as HTMLElement);
    if (i < 0) return;
    const j = e.key === 'ArrowDown' ? i + 1 : e.key === 'ArrowUp' ? i - 1
      : e.key === 'Home' ? 0 : e.key === 'End' ? items.length - 1 : -1;
    if (j < 0 || j >= items.length) return;
    e.preventDefault();
    const to = items[j];
    if (to.classList.contains('bl-srow')) this.selectSeason(+to.dataset.season!);
    for (const it of items) it.tabIndex = it === to ? 0 : -1;
    to.focus();
  }
}

function sent(el: Element | null): void {
  if (!el || el.classList.contains('is-sent')) return;
  el.classList.add('is-sent');
  el.addEventListener('animationend', () => el.classList.remove('is-sent'), { once: true });
}

let modal: BuildsModal | null = null;

export function isOpen(): boolean {
  return !!modal?.isOpen();
}

export function open(ctx: OpenContext): void {
  modal ??= new BuildsModal(ctx.data, ctx.styled);
  modal.open(ctx);
}
