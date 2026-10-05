import { TONE_LABEL, toneOf } from '../../data/tones';
import type { Availability, Status } from './availability';
import { art, type Catalog, type Version } from './catalog';
import { I_BACK, I_CHEV, I_DOWN, I_LOST, I_OFF, I_X, SPRITE, logoHTML } from './icons';
import { ARCH, ARM_NOTE, MINE, PLAT_NAME, type Platform } from './platforms';
import { downloadUrl, type Sizes } from './sizes';
import { esc, spokenSize } from './util';

export const SHELL =
  SPRITE +
  '<aside class="bl-side">' +
    '<div class="bl-side-head">' +
      '<h2 class="bl-side-t">Builds</h2>' +
      '<button class="bl-iconbtn bl-close bl-close-side" type="button" aria-label="Close the builds list">' + I_X + '</button>' +
    '</div>' +
    '<nav class="bl-list" id="bl-list" aria-label="Seasons"></nav>' +
  '</aside>' +
  '<section class="bl-canvas" id="bl-canvas">' +
    '<header class="bl-head" id="bl-head">' +
      '<div class="bl-hero no-art" aria-hidden="true"></div>' +
      '<div class="bl-topbar">' +
        '<button class="bl-back" type="button">' + I_BACK + 'Seasons</button>' +
        '<button class="bl-iconbtn bl-close" type="button" aria-label="Close the builds list">' + I_X + '</button>' +
      '</div>' +
      '<div class="bl-headin" id="bl-headin"></div>' +
    '</header>' +
    '<div class="bl-scroll" id="bl-scroll">' +
      '<div class="bl-detail" id="bl-detail"></div>' +
    '</div>' +
  '</section>' +
  '<p class="bl-say" role="status"></p>';

const WORD: Record<Status, string> = {
  available: 'on the mirror', unavailable: 'copy known elsewhere, not on the mirror',
  missing: 'no copy known anywhere', unknown: 'unverified',
};
const HOW = { source: 'a source names this platform', timeline: 'released while the platform was supported' };

const OFF: Record<Exclude<Status, 'available'>, [string, string]> = {
  unavailable: ['Unavailable', 'a copy is known elsewhere, not on our mirror'],
  missing: ['Lost', 'this build existed on this platform, but no copy is known anywhere'],
  unknown: ['Unverified', 'the mirror could not be reached, so nothing is confirmed'],
};
const CARD_WORD: Record<Exclude<Status, 'available'>, string> = { unavailable: 'not mirrored', missing: 'no copy known', unknown: 'unverified' };
const MENU_WORD: Record<Status, string> = { available: 'Download', unavailable: 'Unavailable', missing: 'Lost', unknown: 'Unverified' };

const archOf = (p: Platform) => (ARCH[p] ? ' ' + ARCH[p] : '');

export class Templates {
  constructor(
    private readonly cat: Catalog,
    private readonly live: Availability,
    private readonly sizes: Sizes,
  ) {}

  private row(n: number, current: number | null): string {
    const tile = `<span class="bl-th"><img data-src="${art(n, '-128')}" data-srcset="${art(n, '-128')} 128w, ${art(n, '-320')} 320w" sizes="64px" alt="" width="64" height="36" loading="lazy" decoding="async"></span>`;
    return '<button class="bl-srow' + (this.live.isDead(n) ? ' is-dead' : '') + '" type="button" data-nav data-season="' + n +
      '" aria-current="' + (n === current) + '" tabindex="' + (n === current ? 0 : -1) + '" aria-label="' + esc(this.live.rowAria(n)) + '">' +
      tile + '<span class="bl-tx"><span class="bl-nm">' + esc(this.cat.rowTitle(n)) + '</span><span class="bl-ft">' + esc(this.cat.rowSub(n)) + '</span></span></button>';
  }

  rail(current: number | null): string {
    let h = '', ch = -1;
    for (const n of this.cat.order) {
      if (this.cat.chapter(n) !== ch) {
        ch = this.cat.chapter(n);
        h += '<div class="bl-chap" role="presentation"><span>' + this.cat.chapterName(ch) + '</span><b>' + this.cat.chapterYears(ch) + '</b></div>';
      }
      h += this.row(n, current);
    }
    return h;
  }

  head(n: number): string {
    const tone = toneOf(n);
    return '<p class="bl-eye">' + this.cat.eyebrow(n) + '</p>' +
      '<h3 class="bl-title" id="bl-title" tabindex="-1">' + esc(this.cat.name(n)) + '</h3>' +
      '<p class="bl-answer"><span class="bl-play ' + tone + '">' + TONE_LABEL[tone] + '.</span></p>';
  }

  body(n: number): string {
    if (this.live.state === 'failed') {
      return '<div class="bl-error" role="alert"><p class="bl-error-t">Couldn’t reach the mirror.</p>' +
        '<p>The builds can’t be shown until the mirror answers; otherwise every download would be a guess.</p>' +
        '<button class="btn btn-ghost bl-retry" type="button"><span>Try again</span></button></div>';
    }
    return '<div class="bl-grid">' + this.cat.versions.get(n)!.map((g, k) => this.card(g, k)).join('') + '</div>';
  }

  private card(g: Version, k: number): string {
    const sel = defaultPlat(g), primary = this.cat.clOf(g);
    const id = '<div class="bl-id"><div class="bl-v">' + esc(g.v) + '</div><div class="bl-meta">' + metaHTML(this.cat.clFor(g, sel)) + '</div></div>';
    const named = 'data-v="' + esc(g.v) + '" data-sel="' + sel + '" tabindex="-1" aria-label="Fortnite ' + esc(g.v) + (primary ? ', changelist ' + esc(primary) : '');
    if (this.live.state === 'loading') {
      return '<article class="bl-card is-loading" style="--i:' + k + '" ' + named + ', checking the mirror" aria-busy="true">' +
        '<div class="bl-card-row">' + id + '<div class="bl-act" aria-hidden="true"><span class="bl-split bl-skel"></span></div></div></article>';
    }
    const fate = this.live.fateOf(g);
    return '<article class="bl-card is-' + fate + '" style="--i:' + k + '" ' + named + ', ' + (fate === 'available' ? 'download' : CARD_WORD[fate]) + '">' +
      '<div class="bl-card-row">' + id + this.action(g, sel) + '</div></article>';
  }

  meta(g: Version, p: Platform): string {
    return metaHTML(this.cat.clFor(g, p));
  }

  action(g: Version, p: Platform): string {
    const f = g.x.find((x) => x.p === p), i = g.b[p];
    const st: Status = f ? this.live.facetStatus(f.has, i) : 'missing';
    let go: string;
    if (st === 'available' && i != null) go = this.download(i);
    else {
      const [word, why] = OFF[st as Exclude<Status, 'available'>] ?? OFF.missing;
      go = '<span class="bl-go is-' + st + '" title="' + esc(word + ' · ' + PLAT_NAME[p] + archOf(p) + ': ' + why) + '">' + (st === 'missing' ? I_LOST : I_OFF) +
        '<span class="bl-t"><b>' + word + '</b>' + fineHTML(p) + '</span></span>';
    }
    const pick = '<button type="button" class="bl-sel" aria-haspopup="menu" aria-expanded="false" title="Choose a platform" aria-label="' +
      esc('Platform: ' + PLAT_NAME[p] + '. Choose another') + '">' + I_CHEV + '</button>';
    return '<div class="bl-act"><div class="bl-split is-' + st + '">' + go + pick + '</div></div>';
  }

  // The only <a> a card can render, and only for a copy confirmed on the mirror.
  private download(i: number): string {
    const k = this.cat.keyOf(i), p = this.cat.platformOf(i), size = this.sizes.of(i);
    return '<a class="bl-go is-available" href="' + downloadUrl(k) + '" rel="noopener" data-k="' + esc(k) +
      '" title="' + esc(downloadTitle(size)) + '" aria-label="Download Fortnite ' + esc(this.cat.D.B.v[i]) + ' for ' + PLAT_NAME[p] + spokenSize(size) + '.">' + I_DOWN +
      '<span class="bl-t"><b>Download</b>' + fineHTML(p) + '</span></a>';
  }

  menu(g: Version, sel: string): string {
    let h = '', k = 0;
    for (const f of g.x) {
      const i = g.b[f.p], st = this.live.facetStatus(f.has, i), a = st === 'available' && i != null;
      const why = st === 'missing' && i != null ? this.cat.whyMissing(i) : '';
      const title = PLAT_NAME[f.p] + archOf(f.p) + (f.cl ? ' · CL ' + f.cl : '') + ' · ' + WORD[st] + (why ? ' · ' + why : '') +
        ' · ' + HOW[f.how] + (f.arm ? ' · ' + ARM_NOTE : '');
      const size = a ? this.sizes.of(i) : '';
      const open = a
        ? '<a href="' + downloadUrl(this.cat.keyOf(i)) + '" rel="noopener" data-k="' + esc(this.cat.keyOf(i)) + '" aria-label="' + esc('Download Fortnite ' + g.v + ' for ' + PLAT_NAME[f.p] + spokenSize(size)) + '."'
        : '<button type="button"';
      h += open + ' class="bl-mi ' + st + '" style="--i:' + (k++) + '" role="menuitemradio" aria-checked="' + (f.p === sel) + '" data-plat="' + f.p + '" tabindex="-1" title="' + esc(a ? (size || 'size unknown') + ' · ' + title : title) + '">' +
        logoHTML(f.p) + '<span class="bl-mn">' + PLAT_NAME[f.p] + (ARCH[f.p] ? '<small>' + ARCH[f.p] + '</small>' : '') + (f.arm ? '<small class="bl-arm">+ Arm</small>' : '') + '</span>' +
        '<span class="bl-ms">' + MENU_WORD[st] + '</span>' +
        '<span class="bl-vh">, ' + WORD[st] + (f.arm ? ', also on Windows on Arm under emulation' : '') + '</span>' +
        (a ? '</a>' : '</button>');
    }
    return h;
  }
}

export const downloadTitle = (size: string): string => 'Download · ' + (size || 'size unknown');

function defaultPlat(g: Version): Platform {
  const has = (p: Platform) => g.x.some((f) => f.p === p);
  if (has(MINE)) return MINE;
  if (has('windows')) return 'windows';
  return g.x.length ? g.x[0].p : 'windows';
}

const metaHTML = (cl: string) => (cl ? '<code>CL ' + esc(cl) + '</code>' : '<span class="bl-nocl">CL unknown</span>');

const fineHTML = (p: Platform) => '<small>' + logoHTML(p) + '<span class="bl-pn">' + PLAT_NAME[p] + '</span></small>';
