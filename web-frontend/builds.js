// The builds launcher. This module is the whole modal: it is fetched, parsed and run only
// when somebody shows intent, so nothing here is on the landing page's critical path.
//
// Two panes, like a store: every season down the left, the chosen one as a page on the
// right. The unit on the page is the VERSION: the builds that share a version string in a
// season collapse into one card whose facets are EVERY platform the version ever shipped on
// (payload V, one letter per platform in X), not only the ones the catalogue holds a copy of.
// A facet is available (that copy is on our mirror), unavailable (a copy is known in some
// other archive), missing (it existed, and no copy is known anywhere) or unknown (the mirror
// could not be asked). Only a catalogued row can ever be available.
//
// The one rule the rest of this file exists to enforce: what is downloadable is only ever
// asserted by the bucket's own index. It is never inferred, never remembered, and never
// derived from `liveSet.size === 0`: that derivation is exactly how a failed fetch turns
// into a page confidently announcing that 490 builds do not exist. `statusOf` takes the
// live state as state, so a failure degrades this to a catalogue, which is an honest
// description of what it still is.

const BASE = 'https://builds.rebootfn.org/';
// Every platform Fortnite ever shipped on, in facet order. Linux never was official, so it
// is not here. D.X carries the payload's own positions for the V codes; PLATS is the order.
const PLATS = ['windows', 'mac', 'ios', 'android', 'switch', 'ps4', 'ps5', 'xbox_one', 'xbox_series'];
// The architecture rides on the chip as a small tag. Every Mac build is x86: Mac support ended
// at 13.40 in August 2020, before any Apple Silicon Mac shipped. Windows on Arm is NOT a build:
// from 38.00 (Nov 2025) the same x64 client runs there under Microsoft's Prism with only the
// anti-cheat native; Epic's own EOS notes (May 2026) say native Arm64 game binaries are not
// supported, and the live manifests through 42.10 carry x64 only. So the Windows chip gets a
// second tag from 38.00 on, and there is no Arm chip to invent.
const ARCH = { windows: 'x86', mac: 'x86' };
const ARM_NOTE = 'also playable on Windows on Arm from 38.00: the x64 client under Prism emulation, with a native Easy Anti-Cheat; no separate Arm package exists';
const PLAT_NAME = { windows: 'Windows', mac: 'macOS', ios: 'iOS', android: 'Android', switch: 'Switch',
                    ps4: 'PS4', ps5: 'PS5', xbox_one: 'Xbox One', xbox_series: 'Xbox Series' };
const PLAY = { stable: 'Plays great', partial: 'Works, with a few rough edges',
               unstable: 'Experimental, expect bugs' };
// The facet's spoken word, and the state's shape. Filled dot = on the mirror, ring = a copy
// is known elsewhere, dashed square = existed but no copy is known (absence, not an error),
// dotted ring = the mirror could not be asked. The word is never the colour's job.
const WORD = { available: 'on the mirror', unavailable: 'copy known elsewhere, not on the mirror',
               missing: 'no copy known anywhere', unknown: 'unverified' };
const HOW = { source: 'a source names this platform', timeline: 'released while the platform was supported' };
// Platform logos: Apple, Android and PlayStation from Simple Icons (CC0); Windows, Switch and
// Xbox drawn here, since Simple Icons no longer ships them. The logo is the platform's identity;
// the chip around it is the status, and the hidden word says it.
const LOGO_OF = { windows: 'windows', mac: 'apple', ios: 'apple', android: 'android', switch: 'switch', ps4: 'ps', ps5: 'ps', xbox_one: 'xbox', xbox_series: 'xbox' };
// Nine chips on a card and nine more on every search hit is the same path, over and over:
// as inline SVG the worst rail render was 1.4 MB of markup. The paths are written once into
// a sprite in the shell, and every chip is a 60-byte <use> of it.
const LOGO_PATHS = {
  windows: '<path d="M3 4.5l7.5-1v8H3zM11.5 3.3 21 2v9.5h-9.5zM3 12.5h7.5v8L3 19.5zM11.5 12.5H21V22l-9.5-1.3z"/>',
  apple: '<path d="M12.152 6.896c-.948 0-2.415-1.078-3.96-1.04-2.04.027-3.91 1.183-4.961 3.014-2.117 3.675-.546 9.103 1.519 12.09 1.013 1.454 2.208 3.09 3.792 3.039 1.52-.065 2.09-.987 3.935-.987 1.831 0 2.35.987 3.96.948 1.637-.026 2.676-1.48 3.676-2.948 1.156-1.688 1.636-3.325 1.662-3.415-.039-.013-3.182-1.221-3.22-4.857-.026-3.04 2.48-4.494 2.597-4.559-1.429-2.09-3.623-2.324-4.39-2.376-2-.156-3.675 1.09-4.61 1.09zM15.53 3.83c.843-1.012 1.4-2.427 1.245-3.83-1.207.052-2.662.805-3.532 1.818-.78.896-1.454 2.338-1.273 3.714 1.338.104 2.715-.688 3.559-1.701"/>',
  android: '<path d="M18.4395 5.5586c-.675 1.1664-1.352 2.3318-2.0274 3.498-.0366-.0155-.0742-.0286-.1113-.043-1.8249-.6957-3.484-.8-4.42-.787-1.8551.0185-3.3544.4643-4.2597.8203-.084-.1494-1.7526-3.021-2.0215-3.4864a1.1451 1.1451 0 0 0-.1406-.1914c-.3312-.364-.9054-.4859-1.379-.203-.475.282-.7136.9361-.3886 1.5019 1.9466 3.3696-.0966-.2158 1.9473 3.3593.0172.031-.4946.2642-1.3926 1.0177C2.8987 12.176.452 14.772 0 18.9902h24c-.119-1.1108-.3686-2.099-.7461-3.0683-.7438-1.9118-1.8435-3.2928-2.7402-4.1836a12.1048 12.1048 0 0 0-2.1309-1.6875c.6594-1.122 1.312-2.2559 1.9649-3.3848.2077-.3615.1886-.7956-.0079-1.1191a1.1001 1.1001 0 0 0-.8515-.5332c-.5225-.0536-.9392.3128-1.0488.5449zm-.0391 8.461c.3944.5926.324 1.3306-.1563 1.6503-.4799.3197-1.188.0985-1.582-.4941-.3944-.5927-.324-1.3307.1563-1.6504.4727-.315 1.1812-.1086 1.582.4941zM7.207 13.5273c.4803.3197.5506 1.0577.1563 1.6504-.394.5926-1.1038.8138-1.584.4941-.48-.3197-.5503-1.0577-.1563-1.6504.4008-.6021 1.1087-.8106 1.584-.4941z"/>',
  switch: '<path fill-rule="evenodd" d="M9.5 2H7a5 5 0 0 0-5 5v10a5 5 0 0 0 5 5h2.5zM6.9 6.2a1.7 1.7 0 1 1 0 3.4 1.7 1.7 0 0 1 0-3.4zM14.5 2H17a5 5 0 0 1 5 5v10a5 5 0 0 1-5 5h-2.5zM17.1 12.4a1.7 1.7 0 1 1 0 3.4 1.7 1.7 0 0 1 0-3.4z"/>',
  ps: '<path d="M8.984 2.596v17.547l3.915 1.261V6.688c0-.69.304-1.151.794-.991.636.18.76.814.76 1.505v5.875c2.441 1.193 4.362-.002 4.362-3.152 0-3.237-1.126-4.675-4.438-5.827-1.307-.448-3.728-1.186-5.39-1.502zm4.656 16.241l6.296-2.275c.715-.258.826-.625.246-.818-.586-.192-1.637-.139-2.357.123l-4.205 1.5V14.98l.24-.085s1.201-.42 2.913-.615c1.696-.18 3.785.03 5.437.661 1.848.601 2.04 1.472 1.576 2.072-.465.6-1.622 1.036-1.622 1.036l-8.544 3.107V18.86zM1.807 18.6c-1.9-.545-2.214-1.668-1.352-2.32.801-.586 2.16-1.052 2.16-1.052l5.615-2.013v2.313L4.205 17c-.705.271-.825.632-.239.826.586.195 1.637.15 2.343-.12L8.247 17v2.074c-.12.03-.256.044-.39.073-1.939.331-3.996.196-6.038-.479z"/>',
  xbox: '<path d="M12 8.7C10.2 6.9 8.3 5.5 6.3 4.5a10 10 0 0 1 11.4 0c-2 1-3.9 2.4-5.7 4.2zM12 13.3c1.9 2.5 4.2 4.8 6.7 6.6a10 10 0 0 1-13.4 0c2.5-1.8 4.8-4.1 6.7-6.6zM9.8 11c-1.5-2.3-3.3-4.3-5.3-5.9a10 10 0 0 0-.7 12.7C5.5 15.5 7.5 13.2 9.8 11zM14.2 11c1.5-2.3 3.3-4.3 5.3-5.9a10 10 0 0 1 .7 12.7c-1.7-2.3-3.7-4.6-6-6.8z"/>',
};
const SPRITE = '<svg class="bl-sprite" hidden aria-hidden="true">' +
  Object.keys(LOGO_PATHS).map((k) => '<symbol id="bl-l-' + k + '" viewBox="0 0 24 24">' + LOGO_PATHS[k] + '</symbol>').join('') + '</svg>';
const logoHTML = (p) => '<svg class="bl-logo" aria-hidden="true"><use href="#bl-l-' + LOGO_OF[p] + '"/></svg>';
const I_DOWN = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M8 2v9M4 7l4 4 4-4M3 13h10"/></svg>';
const I_X = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true"><path d="M4 4l8 8M12 4l-8 8"/></svg>';
const I_BACK = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 3 5 8l5 5"/></svg>';
const I_CHEV = '<svg class="bl-chev" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 6l4 4 4-4"/></svg>';
// The platform this browser runs on, read once. Windows is the answer for everything the
// mirror does not distinguish (Linux, ChromeOS, an unknown UA): it is the mirror's own
// platform, and the launcher's.
const MINE = (() => {
  const d = navigator.userAgentData, pf = (d && d.platform) || navigator.platform || '', ua = navigator.userAgent || '';
  if (/android/i.test(pf) || /android/i.test(ua)) return 'android';
  if (/iphone|ipad|ipod/i.test(pf + ua) || (/mac/i.test(pf) && navigator.maxTouchPoints > 1)) return 'ios';
  if (/mac/i.test(pf)) return 'mac';
  return 'windows';
})();
// The emergency sheet, for the case where builds.css itself did not arrive: two panes, a
// scrolling list, a scrolling page, and nothing else.
const FALLBACK = '.bl{width:min(1220px,94vw);height:86vh;padding:0;color:var(--ink);background:var(--panel);border:1px solid var(--line-strong);display:grid;grid-template-columns:280px minmax(0,1fr);overflow:hidden}.bl-side{display:flex;flex-direction:column;min-height:0;border-right:1px solid var(--line)}.bl-side-head{padding:12px 16px}.bl-side-t{margin:0;font-size:20px}.bl-list{flex:1;overflow-y:auto}.bl-srow{display:block;width:100%;text-align:left;padding:6px 12px;color:inherit;background:none;border:0;font:inherit}.bl-th img{display:none}.bl-canvas{display:flex;flex-direction:column;min-height:0;position:relative}.bl-scroll{flex:1;overflow-y:auto;min-height:0}.bl-hero{display:none}.bl-topbar{display:flex;justify-content:space-between;padding:12px 16px}.bl-detail{padding:0 24px 32px}.bl-title{font-size:32px;margin:6px 0 12px}.bl-grid{display:grid;gap:10px;grid-template-columns:repeat(auto-fill,minmax(260px,1fr))}.bl-card{padding:12px;border:1px solid var(--line);border-radius:6px}.bl-go{display:inline-block;padding:6px 12px;background:var(--storm);color:#04121C;border-radius:6px;text-decoration:none;font-weight:700}.bl-vh,.bl-say{position:absolute;clip-path:inset(50%)}html.bl-lock{overflow:hidden;padding-right:var(--bl-sbw,0px)}.bl:not([open]){display:none!important}.bl-close-side{display:none}@media(max-width:760px){.bl{grid-template-columns:1fr}.bl-canvas{display:none}.bl.is-pushed .bl-side{display:none}.bl.is-pushed .bl-canvas{display:block}.bl-close-side{display:inline-block}}';

let D = null;                 // decoded payload
let bySeason = null;          // Map<number, number[]>, 0 (pre-release) first
let versions = null;          // Map<number, {v, b:{plat:i}, idx:number[], x:facet[]}[]>, catalogue order
let order = null;             // number[], season keys in list order
let allKeys = null;           // Set<string>, every catalogue key (orphan detection)
let N = 0, NV = 0;

let liveSet = new Set();
let liveState = 'loading';    // 'loading' | 'ok' | 'failed'

let dlg, side, list, canvas, head, headin, scroller, hero, detail, sayEl, banner = null;
let season = null, heroN = null, opener = null, retryFn = null;
let pushed = false, popping = false, warmed = false, suppress = false, shieldT = 0;
let viewPushed = false, depth = 0;   // phone stack: is the page on stage, and did it push a history entry
let probed = new Set(), sizeText = new Map();
let io = null;
let menu = null, menuCard = null;   // the one open platform menu, and the card it belongs to
let stripOwed = false;        // a close is traversing history: strip the modal hash when it lands
let place = null, restore = 'auto';   // where the reader was, and the entry's own scroll mode
let fromId = '', hid = null;          // the page's own fragment at open, and its element while the close hides it

const phoneMQ = matchMedia('(max-width: 760px)');
const isPhone = () => phoneMQ.matches;
const reduceMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches;
const glide = () => (reduceMotion() ? 'auto' : 'smooth');
const ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ESCAPES[c]);
const pad2 = (n) => (n < 10 ? '0' + n : '' + n);
const hashFor = (n) => (n === 0 ? '#season-pre' : '#season-' + n);
const plural = (n, w) => n + ' ' + w + (n === 1 ? '' : 's');
const art = (n, sz) => 'seasons/s' + pad2(n) + (sz || '') + '.webp';
// Two buckets sit outside the seasons: 0 is the Online Tests, the 2014-2017 closed alpha,
// with the OT6.5 splash as its art; 99 is the internal builds (dev and certification
// builds known mostly by changelist) with a plain tile that says exactly that.
const INTERNAL = 99;
const BUCKET = {
  0:  { name: 'Online Tests', sub: 'Alpha builds', tone: 'unstable', eye: 'Alpha <span>·</span> Online Tests <span>·</span> Dec 2014 – Jul 2017' },
  99: { name: 'Internal builds', sub: 'Dev builds', tone: 'unstable', eye: 'Internal <span>·</span> dev and certification builds <span>·</span> 2017 – 2022' },
};

/* ---------- Decode ---------- */

// 565 of 589 keys are `{platform}-{version}.{ext}`. The other 24 ship an explicit stem in
// `ko`, and the extension varies per record (480 zip / 109 rar), not per platform, so
// reconstructing a key from platform + version alone is wrong for 24 builds.
const keyOf = (i) => (D.B.ko[i] ?? `${D.P[D.B.p[i]]}-${D.B.v[i]}`) + '.' + D.E[D.B.e[i]];

// The version's platforms, from its V code: one letter per position of D.X; '.' never
// existed there, A/a a copy is known somewhere, M/m it existed and no copy is known; upper
// case when a source names the platform, lower case when only the support timeline does.
// Ordered as PLATS. A payload without V (an older build) falls back to the catalogue's own
// rows, where the route says whether a copy is known.
function facetsOf(v, rows) {
  const code = D.V && D.V[v], out = [];
  for (const p of PLATS) {
    let c = code ? code[D.X.indexOf(p)] : undefined;
    if (!code && p in rows) c = D.R[D.B.r[rows[p]]] === 'missing' ? 'M' : 'A';
    if (!c || c === '.') continue;
    const cl = D.C && D.C[v] ? D.C[v][D.X.indexOf(p)] : undefined;
    const arm = p === 'windows' && !!code && D.X.indexOf('windows_arm') >= 0 && code[D.X.indexOf('windows_arm')] !== '.';
    out.push({ p, has: c === 'a' || c === 'A', how: c === c.toUpperCase() ? 'source' : 'timeline', cl: cl || null, arm });
  }
  return out;
}
// A catalogue row's own has-a-copy fact, the same way: one letter, or the route.
function hasCopy(i) {
  const code = D.V && D.V[D.B.v[i]], c = code && code[D.X.indexOf(D.P[D.B.p[i]])];
  return c ? c === 'a' || c === 'A' : D.R[D.B.r[i]] !== 'missing';
}
function decode(data) {
  D = data;
  const B = D.B;
  N = B.v.length;
  allKeys = new Set();
  bySeason = new Map([[0, []]]);
  versions = new Map();
  order = [0];
  let max = 0;
  for (const k in D.S) if (+k > max) max = +k;
  // The two non-season buckets sit together at the top, then the chapters in order.
  if (D.S[INTERNAL]) { bySeason.set(INTERNAL, []); order.push(INTERNAL); }
  for (let s = 1; s <= max; s++) if (D.S[s] && s !== INTERNAL) { bySeason.set(s, []); order.push(s); }
  for (let i = 0; i < N; i++) {
    const s = B.s[i], k = keyOf(i);
    bySeason.get(s).push(i);
    allKeys.add(k);
  }
  // The version groups are a catalogue fact, so they are cut once. The payload is already
  // sorted by season then version, so first-seen order is the right order, comparator-free.
  NV = 0;
  for (const [s, idx] of bySeason) {
    const out = [], at = new Map();
    for (const i of idx) {
      let g = at.get(B.v[i]);
      if (!g) { g = { v: B.v[i], b: {}, idx: [] }; at.set(B.v[i], g); out.push(g); }
      g.b[D.P[B.p[i]]] = i;
      g.idx.push(i);
    }
    for (const g of out) g.x = facetsOf(g.v, g.b);
    versions.set(s, out);
    NV += out.length;
  }
}

/* ---------- Status: guessing is structurally impossible ---------- */

// A facet's status. `has` is the catalogue fact (a copy is known somewhere); `i` is the
// catalogue row for that platform, if there is one; only such a row can be on the mirror.
function facetStatus(has, i) {
  if (liveState === 'ok' && i != null && liveSet.has(keyOf(i))) return 'available';   // the mirror's own word
  if (!has) return 'missing';                       // a catalogue fact, network-independent
  if (liveState !== 'ok') return 'unknown';         // never 'unavailable': that is a guess
  return 'unavailable';
}
const statusOf = (i) => facetStatus(hasCopy(i), i);
// The version's primary changelist: c[0] of any of its rows, the Windows one where the
// platforms disagree (the generator puts it first; the facets carry the others).
const clOf = (g) => (D.B.c[g.idx[0]] ? D.B.c[g.idx[0]].split(' ')[0] : '');

function fmtSize(mib) {
  if (!mib) return null;                            // blank, never "0 B"
  const gb = (mib * 1048576) / 1e9;                 // decimal GB: what content-length reports
  if (gb >= 100) return Math.round(gb) + ' GB';
  if (gb >= 10) return gb.toFixed(1) + ' GB';
  if (gb >= 1) return gb.toFixed(2) + ' GB';
  return Math.round((mib * 1048576) / 1e6) + ' MB';
}

/* ---------- Fate ---------- */

// The same three lines cardHTML() draws its class from, factored out so the row and the card
// can never disagree about what a version's fate is.
function fateOf(g) {
  let unknown = false, unavailable = false;
  for (const f of g.x) {
    const s = facetStatus(f.has, g.b[f.p]);
    if (s === 'available') return 'available';
    if (s === 'unknown') unknown = true;
    else if (s === 'unavailable') unavailable = true;
  }
  return unknown ? 'unknown' : unavailable ? 'unavailable' : 'missing';
}

/* ---------- Season facts ---------- */

// Everything the sidebar row, the answer sentence and the sections need, in one pass.
function seasonStats(n) {
  const vs = versions.get(n);
  const s = { versions: vs.length, dl: 0, allLost: 0 };
  for (const g of vs) {
    let anyAvail = false, anyLive = false;
    for (const f of g.x) {
      const st = facetStatus(f.has, g.b[f.p]);
      if (st === 'available') anyAvail = true;
      if (st !== 'missing') anyLive = true;
    }
    if (anyAvail) s.dl++;
    if (!anyLive) s.allLost++;
  }
  return s;
}

// seasonStats walks every group and facet of a season, and the rail asks for all 44 of them
// on every keystroke. The answer only ever moves when the live answer does, so it is cached
// against an epoch that applyLive and retryLive bump.
let statsCache = new Map(), statsEpoch = 0;
function statsOf(n) {
  const k = n + ':' + statsEpoch;
  let s = statsCache.get(k);
  if (!s) { s = seasonStats(n); statsCache.set(k, s); }
  return s;
}

// order starts with the two non-season buckets, so order[1] is Internal builds, not a
// season: the bucket-less entry points (#builds, "Browse builds") land on the first real one.
const firstSeason = () => order.find((n) => !BUCKET[n]) ?? order[0];
const seasonName = (n) => (BUCKET[n] ? BUCKET[n].name : D.S[n].name);
const chapOf = (n) => (BUCKET[n] ? 0 : D.S[n].ch);   // both buckets share group 0
const chapName = (ch) => (ch ? 'Chapter ' + ch : 'Outside the chapters');

function chapYears(ch) {
  if (!ch) return '';   // the two buckets outside the chapters carry no year span
  let lo = 0, hi = 0;
  for (const s of order) {
    if (chapOf(s) !== ch || !s) continue;
    const m = /\d{4}/.exec(D.S[s].date);
    if (!m) continue;
    const y = +m[0];
    if (!lo || y < lo) lo = y;
    if (y > hi) hi = y;
  }
  return !lo ? '' : lo === hi ? '' + lo : lo + '–' + hi;
}

// The tile tag: S6, SX, MS1, OG, RMX: short enough for a 64px thumbnail.
// The row reads "Season 6" over "Darkness Rises": the number is what people navigate by,
// the name is what they remember. Chapter 1's first three carry the wiki's names.
const rowTitle = (n) => (BUCKET[n] ? BUCKET[n].name : D.S[n].label);
const rowSub = (n) => (BUCKET[n] ? BUCKET[n].sub : D.S[n].name === D.S[n].label ? '' : D.S[n].name);

function rowAria(n, st) {
  const who = n ? (D.S[n].label === D.S[n].name ? D.S[n].label
                    : D.S[n].name + ', Chapter ' + D.S[n].ch + ' ' + D.S[n].label)
                : 'Pre-release and Open Test builds';
  return who + '. ' + factOf(n, st) + (liveState === 'ok' ? '.' : '. Availability unverified.');
}

// The row's one line. Only a confirmed answer may say "to play" or "nothing mirrored"; a
// failed or pending check falls back to the catalogue count, which is always true.
function factOf(n, st) {
  if (liveState !== 'ok') return plural(st.versions, 'version');
  if (st.dl) return st.dl + ' to play';
  if (st.allLost === st.versions) return 'all lost';
  return 'nothing mirrored';
}

// Playability is a fact about the season that the landing page already states in words.
// It is carried across the dialog boundary from the opener's own tone class.
const toneCache = new Map();
function tone(n) {
  if (BUCKET[n]) return BUCKET[n].tone;   // alpha and dev builds: experimental by definition
  if (toneCache.has(n)) return toneCache.get(n);
  const card = document.querySelector('.film[data-season="' + n + '"]');
  let t = '';
  if (card) for (const c of ['stable', 'partial', 'unstable']) if (card.classList.contains(c)) t = c;
  toneCache.set(n, t);
  return t;
}

/* ---------- Shell ---------- */

function buildShell(styled) {
  // The one stylesheet this file ever writes is the emergency one, for the case where
  // builds.css itself did not arrive.
  if (styled === false) {
    const fb = document.createElement('style');
    fb.textContent = FALLBACK;
    document.head.appendChild(fb);
  }

  dlg = document.createElement('dialog');
  // 'bl-dialog', not 'builds': the page owns #builds, a static anchor that gives the
  // #builds links somewhere real to land when this module never runs.
  dlg.id = 'bl-dialog';
  dlg.className = 'bl';
  dlg.setAttribute('aria-label', 'Builds');
  dlg.innerHTML =
    SPRITE +
    '<aside class="bl-side">' +
      '<div class="bl-side-head">' +
        '<h2 class="bl-side-t">Builds</h2>' +
        // The close is shown only on a phone, where the list is a whole screen and the canvas
        // (and its close) is off stage and inert.
        '<button class="bl-iconbtn bl-close bl-close-side" type="button" aria-label="Close the builds list">' + I_X + '</button>' +
      '</div>' +
      '<nav class="bl-list" id="bl-list" aria-label="Seasons"></nav>' + 
    '</aside>' +
    // The header (art, controls, eyebrow, title, answer) is fixed; only the versions
    // scroll, in their own scroller beneath it.
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
  document.body.appendChild(dlg);

  side = dlg.querySelector('.bl-side');
  list = dlg.querySelector('.bl-list');
  canvas = dlg.querySelector('.bl-canvas');
  hero = dlg.querySelector('.bl-hero');
  detail = dlg.querySelector('.bl-detail');
  head = dlg.querySelector('.bl-head');
  headin = dlg.querySelector('.bl-headin');
  scroller = dlg.querySelector('.bl-scroll');
  bindPull();
  sayEl = dlg.querySelector('.bl-say');

  dlg.addEventListener('click', onClick);
  dlg.addEventListener('keydown', onKey);
  list.addEventListener('keydown', onListKey);
  // Engines that fire `cancel` without a keydown we see get the same close as Escape.
  dlg.addEventListener('cancel', (e) => {
    e.preventDefault();   // the close is ours: the exit plays first, then closeModal calls dlg.close()
    closeModal(false);
  });
  dlg.addEventListener('close', onClose);
  addEventListener('popstate', onPop);
  // Registered after the page's own hashchange listener, so it runs second: the page has
  // already asked isOpen() and been told to leave the closed dialog alone.
  addEventListener('hashchange', () => { if (suppress) { clearTimeout(shieldT); suppress = false; } settle(); });
  phoneMQ.addEventListener('change', syncStack);
}

/* ---------- Sidebar ---------- */

function rowHTML(n) {
  const st = statsOf(n), dead = liveState === 'ok' && !st.dl;
  // The 128 file is the 64×36 box at 2x; a 3x phone takes the 320. Both ride data- attributes
  // so the observer gate (and the inert phone list) decides when a byte is spent.
  const tile = '<span class="bl-th"><img data-src="' + art(n, '-128') + '" data-srcset="' + art(n, '-128') + ' 128w, ' + art(n, '-320') + ' 320w" sizes="64px" alt="" width="64" height="36" loading="lazy" decoding="async"></span>';
  return '<button class="bl-srow' + (dead ? ' is-dead' : '') + '" type="button" data-nav data-season="' + n +
    '" aria-current="' + (n === season) + '" tabindex="' + (n === season ? 0 : -1) + '" aria-label="' + esc(rowAria(n, st)) + '">' +
    tile + '<span class="bl-tx"><span class="bl-nm">' + esc(rowTitle(n)) + '</span><span class="bl-ft">' + esc(rowSub(n)) + '</span></span></button>';
}

const chapHTML = (ch) => '<div class="bl-chap" role="presentation"><span>' + chapName(ch) + '</span><b>' + chapYears(ch) + '</b></div>';

// The rail: chapter headers and season rows, the catalogue's own shape.
function renderSide() {
  let h = '', ch = -1;
  for (const n of order) {
    if (chapOf(n) !== ch) { ch = chapOf(n); h += chapHTML(ch); }
    h += rowHTML(n);
  }
  paintRail(h);
  cascade(list, list.dataset.view !== 'all', [...list.children]);   // the rail cascades on its first paint, never on a rebuild
  list.dataset.view = 'all';
}

// The rail's markup is byte-identical rebuild after rebuild (a live answer only changes row
// classes), so re-parsing it and re-watching 42 thumbnails is skipped when nothing moved.
// Compared as the generated string, never as a read-back: hydrated thumbnails differ in `src`.
let railHTML = '';
function paintRail(h) {
  if (h === railHTML) return;
  railHTML = h;
  list.innerHTML = h;
  watchThumbs();
}

// loading="lazy" alone fetches 1250px past the scroll port: 32 of 42 thumbnails on a phone,
// even when the list is covered and inert. Hydrating from an observer rooted on the list with
// two rows of lookahead is the actual limit; the lazy attribute stays as a second guard.
// The tile stays its panel colour until the file has actually decoded, then the art fades
// in (CSS .is-in): a tile mid-fetch never shows the browser's own broken-image glyph.
function hydrate(img) {
  if (!img.dataset.src) return;
  const inn = () => img.classList.add('is-in');
  img.addEventListener('load', inn, { once: true });
  // srcset first: a bare srcset on a src-less img fetches at once, so it stays deferred like
  // the src, or the observer gate buys nothing.
  if (img.dataset.srcset) { img.srcset = img.dataset.srcset; delete img.dataset.srcset; }
  img.src = img.dataset.src;
  delete img.dataset.src;
  if (img.complete && img.naturalWidth) inn();   // already cached: load may not fire again
}

// The rows on screen (and two rows past it) hydrate at once; the rest of the rail follows in
// idle time, so a fast scroll a moment later lands on art that is already there. Nothing is
// fetched while the list is off stage under the page on a phone.
let warmT = 0;
function warmThumbs() {
  if (warmT) cancelIdleCallback(warmT);
  warmT = requestIdleCallback(() => {
    warmT = 0;
    if (!list || side.inert) return;
    for (const im of list.querySelectorAll('img[data-src]')) hydrate(im);
  }, { timeout: 1500 });
}

function watchThumbs() {
  const imgs = list.querySelectorAll('img[data-src]');
  if (!('IntersectionObserver' in window)) { for (const im of imgs) hydrate(im); return; }
  if (!io) io = new IntersectionObserver((ents) => {
    for (const en of ents) if (en.isIntersecting) { hydrate(en.target); io.unobserve(en.target); }
  }, { root: list, rootMargin: '120px 0px' });
  io.disconnect();
  if (side.inert) return;   // phone, page on stage: nothing under it is worth a byte
  for (const im of imgs) io.observe(im);
  if ('requestIdleCallback' in window) warmThumbs();
}

function markCurrent(n, move) {
  for (const r of list.querySelectorAll('.bl-srow')) {
    const on = +r.dataset.season === n;
    r.setAttribute('aria-current', on ? 'true' : 'false');
    r.tabIndex = on ? 0 : -1;
    // CSS scroll-behavior does not govern JS-initiated scrolls, so reduced motion has to
    // be honoured explicitly here, as the reels IIFE on the page already does.
    if (on && move) r.scrollIntoView({ block: 'nearest', behavior: glide() });
  }
}

/* ---------- Detail ---------- */

// One chip per platform the version existed on. The title carries what the chip cannot:
// that platform's own changelist where it differs from the card's, why we say it existed,
// and the catalogue's reason when it is missing. mini: shape only (the search hits), with
// the name moved into the hidden text so nothing is lost to a screen reader.
// A mini chip is shape alone: the logo on a chip whose surface is the status. It carries no
// title (an 18px square inside a button is not a tooltip target) and no word of its own:
// the hit says all nine platforms in one sentence, and the card's menu spells every one out.
// The platform a card opens on: this browser's own whenever the version existed on it (a
// Mac sees macOS, even when that copy is lost; the menu is one press away), else Windows,
// else the first the version existed on.
function defaultPlat(g) {
  const has = (p) => g.x.some((f) => f.p === p);
  if (has(MINE)) return MINE;
  if (has('windows')) return 'windows';
  return g.x.length ? g.x[0].p : 'windows';
}

// What the card offers for one platform, as a split pill: the action in front, two lines
// deep, the word on top and the platform in fine print beneath, and beside it, a little
// apart, a chevron button that opens the menu of every platform the version existed on. The
// action is the one thing that is ever a link, and only for a copy on the mirror; every other
// fate is a plain, clearly worded pill in the same place.
const OFF = { unavailable: ['Unavailable', 'a copy is known elsewhere, not on our mirror'],
              missing: ['Lost', 'this build existed on this platform, but no copy is known anywhere'],
              unknown: ['Unverified', 'the mirror could not be reached, so nothing is confirmed'] };
// The fine print under the word: the platform's logo and name, and nothing else.
const fineHTML = (p) => '<small>' + logoHTML(p) + '<span class="bl-pn">' + PLAT_NAME[p] + '</span></small>';
function actHTML(g, p) {
  const f = g.x.find((x) => x.p === p), i = g.b[p];
  const st = f ? facetStatus(f.has, i) : 'missing';
  const name = PLAT_NAME[p], arch = ARCH[p] ? ' ' + ARCH[p] : '';
  let go;
  if (st === 'available' && i != null) go = dlHTML(i);
  else {
    const [word, why] = OFF[st] || OFF.missing;
    go = '<span class="bl-go is-' + st + '" title="' + esc(word + ' · ' + name + arch + ': ' + why) + '">' + (st === 'missing' ? I_LOST : I_OFF) +
      '<span class="bl-t"><b>' + word + '</b>' + fineHTML(p) + '</span></span>';
  }
  // The chevron is always there, so every card has the same two parts; a version that
  // existed on one platform opens a one-line menu that says so.
  const sel = '<button type="button" class="bl-sel" aria-haspopup="menu" aria-expanded="false" title="Choose a platform" aria-label="' +
    esc('Platform: ' + name + '. Choose another') + '">' + I_CHEV + '</button>';
  return '<div class="bl-act"><div class="bl-split is-' + st + '">' + go + sel + '</div></div>';
}
const clFor = (g, p) => (D.C && D.C[g.v] && D.C[g.v][D.X.indexOf(p)]) || clOf(g);

function sizeOf(i) {
  const k = keyOf(i);
  return sizeText.has(k) ? sizeText.get(k) : (fmtSize(D.B.b[i]) || '');
}

// The download pill, for a copy on the mirror: the one element in a card that is a link.
// Link-ness is decided by tag at build time: only an 'available' build ever reaches this
// function, so no CSS drift and no class typo can make an unconfirmed build clickable.
function dlHTML(i) {
  const k = keyOf(i), p = D.P[D.B.p[i]], size = sizeOf(i);
  const spoken = / [GM]B$/.test(size) ? ', ' + size.replace(' GB', ' gigabytes').replace(' MB', ' megabytes') : '';
  return '<a class="bl-go is-available" href="' + BASE + encodeURIComponent(k) + '" rel="noopener" data-k="' + esc(k) +
    '" title="' + esc('Download · ' + (size || 'size unknown')) + '" aria-label="Download Fortnite ' + esc(D.B.v[i]) + ' for ' + PLAT_NAME[p] + spoken + '.">' + I_DOWN +
    '<span class="bl-t"><b>Download</b>' + fineHTML(p) + '</span></a>';
}

// One card per version, whatever its fate. All four fates share one footprint (the same
// row, the same subtitle (always the changelist), the same facets line), so the season
// reads as one release timeline; only the style of the card says whether you can have it,
// since the word is spoken only to screen readers (the card's label and each facet's hidden
// text). Nothing but an available build ever renders an <a>.
const CARD_WORD = { unavailable: 'not mirrored', missing: 'no copy known', unknown: 'unverified' };
// The subtitle is the changelist, always; never an empty line.
const metaHTML = (cl) => (cl ? '<code>CL ' + esc(cl) + '</code>' : '<span class="bl-nocl">CL unknown</span>');
const I_OFF = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true"><circle cx="8" cy="8" r="5.5"/></svg>';
const I_LOST = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true"><circle cx="8" cy="8" r="5.5"/><path d="M4.2 11.8 11.8 4.2"/></svg>';

// Choosing a platform on a card: the action and the changelist follow it.
function selectPlat(card, p) {
  const g = groupOf(card);
  if (!g || !g.x.some((x) => x.p === p)) return;
  card.dataset.sel = p;
  card.querySelector('.bl-act').outerHTML = actHTML(g, p);
  card.querySelector('.bl-meta').innerHTML = metaHTML(clFor(g, p));
}

/* ---------- The platform menu ---------- */

// Every platform the version existed on, each with its fate. An available copy is a link
// (picking it downloads, the way a store's "other platforms" list does) and every other
// row is a button that only turns the card to that platform, so its state can be read.
const MENU_WORD = { available: 'Download', unavailable: 'Unavailable', missing: 'Lost', unknown: 'Unverified' };
function menuHTML(g, sel) {
  let h = '', k = 0;
  for (const f of g.x) {
    const i = g.b[f.p], st = facetStatus(f.has, i), a = st === 'available' && i != null;
    const why = st === 'missing' && i != null ? D.W[D.B.w[i]] : '';
    const title = PLAT_NAME[f.p] + (ARCH[f.p] ? ' ' + ARCH[f.p] : '') + (f.cl ? ' · CL ' + f.cl : '') + ' · ' + WORD[st] + (why ? ' · ' + why : '') +
      ' · ' + HOW[f.how] + (f.arm ? ' · ' + ARM_NOTE : '');
    const size = a ? sizeOf(i) : '';
    const spoken = / [GM]B$/.test(size) ? ', ' + size.replace(' GB', ' gigabytes').replace(' MB', ' megabytes') : '';
    h += (a ? '<a href="' + BASE + encodeURIComponent(keyOf(i)) + '" rel="noopener" data-k="' + esc(keyOf(i)) + '" aria-label="' + esc('Download Fortnite ' + g.v + ' for ' + PLAT_NAME[f.p] + spoken) + '."' : '<button type="button"') +
      ' class="bl-mi ' + st + '" style="--i:' + (k++) + '" role="menuitemradio" aria-checked="' + (f.p === sel) + '" data-plat="' + f.p + '" tabindex="-1" title="' + esc(a ? (size || 'size unknown') + ' · ' + title : title) + '">' +
      logoHTML(f.p) + '<span class="bl-mn">' + PLAT_NAME[f.p] + (ARCH[f.p] ? '<small>' + ARCH[f.p] + '</small>' : '') + (f.arm ? '<small class="bl-arm">+ Arm</small>' : '') + '</span>' +
      '<span class="bl-ms">' + MENU_WORD[st] + '</span>' +
      '<span class="bl-vh">, ' + WORD[st] + (f.arm ? ', also on Windows on Arm under emulation' : '') + '</span>' +
      (a ? '</a>' : '</button>');
  }
  return h;
}

function openMenu(card) {
  if (menuCard === card) { closeMenu(true); return; }
  closeMenu();
  const g = groupOf(card);
  if (!g) return;
  menu = document.createElement('div');
  menu.className = 'bl-menu';
  menu.setAttribute('role', 'menu');
  menu.setAttribute('aria-label', 'Platform');
  menu.innerHTML = menuHTML(g, card.dataset.sel);
  const holder = card.querySelector('.bl-act'), pick = holder.querySelector('button.bl-sel');
  holder.appendChild(menu);
  menuCard = card;
  pick.setAttribute('aria-expanded', 'true');
  // Opens downward unless that would run off the bottom of the pane and upward would not.
  const r = holder.getBoundingClientRect(), sr = scroller.getBoundingClientRect(), h = menu.offsetHeight + 8;
  menu.classList.toggle('is-up', sr.bottom - r.bottom < h && r.top - sr.top > h);
  const cur = menu.querySelector('[aria-checked="true"]') || menu.firstElementChild;
  cur.tabIndex = 0;
  cur.focus({ preventScroll: true });
}

function closeMenu(refocus) {
  if (!menu) return;
  const card = menuCard, m = menu;
  menu = null;
  menuCard = null;
  // The menu leaves on the cut beat (CSS .is-out), unpressable while it goes, and is removed
  // once it has gone; reduced motion removes it at once. A pick replaces the whole .bl-act,
  // and the exiting menu goes with it.
  if (reduceMotion()) m.remove();
  else { m.classList.add('is-out'); setTimeout(() => m.remove(), 200); }
  const pick = card.querySelector('button.bl-sel');
  if (pick) { pick.setAttribute('aria-expanded', 'false'); if (refocus) pick.focus({ preventScroll: true }); }
}

// Arrows walk the menu, Home/End jump, Escape closes it back onto its button, Tab leaves it
// (and closes it). Returns true when the key was the menu's.
function onMenuKey(e) {
  if (!menu) return false;
  const inMenu = menu.contains(e.target);
  const onPick = e.target === (menuCard && menuCard.querySelector('button.bl-sel'));
  if (!inMenu && !onPick) return false;
  if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); closeMenu(true); return true; }
  if (e.key === 'Tab') { closeMenu(); return true; }
  const all = [...menu.querySelectorAll('.bl-mi')];
  let k = all.indexOf(document.activeElement);
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

function groupOf(card) {
  const v = card.dataset.v;
  for (const list of versions.values()) for (const g of list) if (g.v === v) return g;
  return null;
}

function cardHTML(g, k) {
  const fate = fateOf(g);
  const sel = defaultPlat(g), primary = clOf(g);
  const id = '<div class="bl-id"><div class="bl-v">' + esc(g.v) + '</div><div class="bl-meta">' + metaHTML(clFor(g, sel)) + '</div></div>';
  // Until the mirror answers, the part of the card that depends on it shimmers: no status
  // is shown that could later be contradicted. The version and changelist are catalogue
  // facts and stay solid.
  if (liveState === 'loading') {
    return '<article class="bl-card is-loading" style="--i:' + k + '" data-v="' + esc(g.v) + '" data-sel="' + sel + '" tabindex="-1" aria-label="Fortnite ' + esc(g.v) +
      (primary ? ', changelist ' + esc(primary) : '') + ', checking the mirror" aria-busy="true">' +
      '<div class="bl-card-row">' + id + '<div class="bl-act" aria-hidden="true"><span class="bl-split bl-skel"></span></div></div></article>';
  }
  return '<article class="bl-card is-' + fate + '" style="--i:' + k + '" data-v="' + esc(g.v) + '" data-sel="' + sel + '" tabindex="-1" aria-label="Fortnite ' + esc(g.v) +
    (primary ? ', changelist ' + esc(primary) : '') + ', ' + (fate === 'available' ? 'download' : CARD_WORD[fate]) + '">' +
    '<div class="bl-card-row">' + id + actHTML(g, sel) + '</div></article>';
}

function answerHTML(n) {
  // Only the playability line: the dot and the legend's own words. Everything else the
  // pane once said here is either on the cards or in the banner.
  const t = tone(n);
  return t ? '<span class="bl-play ' + t + '">' + PLAY[t] + '.</span>' : '';
}

// The failed state is an error, not a degraded list: without the mirror's answer no card can
// say whether its file is there, so none is shown. The season header stays, the sidebar
// stays, and the one control is the retry.
function errorHTML() {
  return '<div class="bl-error" role="alert"><p class="bl-error-t">Couldn’t reach the mirror.</p>' +
    '<p>The builds can’t be shown until the mirror answers; otherwise every download would be a guess.</p>' +
    '<button class="btn btn-ghost bl-retry" type="button"><span>Try again</span></button></div>';
}

function detailHTML(n) {
  const vs = versions.get(n);
  const eyebrow = BUCKET[n] ? BUCKET[n].eye
    : 'Chapter ' + D.S[n].ch + ' <span>·</span> ' + esc(D.S[n].label) + ' <span>·</span> ' + esc(D.S[n].date);
  const answer = answerHTML(n);
  headin.innerHTML = '<p class="bl-eye">' + eyebrow + '</p>' +
    '<h3 class="bl-title" id="bl-title" tabindex="-1">' + esc(seasonName(n)) + '</h3>' +
    (answer ? '<p class="bl-answer">' + answer + '</p>' : '');
  if (liveState === 'failed') return errorHTML();
  let h = '<div class="bl-grid">';
  let k = 0;
  for (const g of vs) h += cardHTML(g, k++);   // release order, every fate in one timeline; k is the cascade index
  return h + '</div>';
}

function setHero(n) {
  if (heroN === n) { landArt(); return; }
  heroN = n;
  hero.classList.remove('is-in');
  hero.classList.remove('no-art');
  // The blur is already in memory from the reel and paints instantly; the real art lands
  // over it a moment later. Both are aria-hidden through the container: the heading says
  // everything they say.
  hero.innerHTML = '<img class="bl-wash" src="' + art(n, '-blur') + '" alt="" width="64" height="36" decoding="async">';
  landArt();
}

// The 640 art is only asked for once the canvas is actually on stage: on the phone's list
// screen the canvas is translated off screen and inert, and the wash is all it needs.
function landArt() {
  const n = heroN;
  if (hero.querySelector('.bl-art') || (isPhone() && !viewPushed)) return;
  hero.insertAdjacentHTML('beforeend', '<img class="bl-art" src="' + art(n) + '" srcset="' + art(n, '-480') + ' 480w, ' + art(n) + ' 640w" sizes="(max-width: 760px) 100vw, 924px" alt="" width="640" height="360" decoding="async">');
  const img = hero.lastElementChild;
  const land = () => { if (heroN === n) hero.classList.add('is-in'); };
  if (img.complete) land(); else img.onload = land;
}

// fresh: a season change (fade the page in, scroll to top); otherwise an in-place rebuild
// that keeps the scroll position; applyLive and retry go through the second door.
// Pulling past the top stretches the art and lets it spring back, the way a launcher's
// poster gives under a thumb. Wheel and touch both feed one number; the CSS turns it into a
// compositor-only scale. Nothing moves under reduced motion, and there is nothing to
// stretch on the pre-release page.
let pull = 0, pullTimer = 0, touchY = -1;
function setPull(v) {
  pull = Math.max(0, Math.min(160, v));
  head.style.setProperty('--bl-pull', pull);
}
function releasePull() { clearTimeout(pullTimer); head.classList.remove('is-pulling'); setPull(0); }
const canPull = () => !reduceMotion() && !hero.classList.contains('no-art') && scroller.scrollTop === 0;
function bindPull() {
  scroller.addEventListener('wheel', (e) => {
    if (!canPull() || e.deltaY >= 0) { if (pull) releasePull(); return; }
    head.classList.add('is-pulling');
    setPull(pull - e.deltaY * 0.55);
    clearTimeout(pullTimer);
    pullTimer = setTimeout(releasePull, 140);
  }, { passive: true });
  scroller.addEventListener('touchstart', (e) => { touchY = scroller.scrollTop === 0 ? e.touches[0].clientY : -1; }, { passive: true });
  scroller.addEventListener('touchmove', (e) => {
    if (touchY < 0 || !canPull()) return;
    const dy = e.touches[0].clientY - touchY;
    if (dy <= 0) { if (pull) releasePull(); return; }
    head.classList.add('is-pulling');
    setPull(dy * 0.5);
  }, { passive: true });
  scroller.addEventListener('touchend', () => { touchY = -1; releasePull(); }, { passive: true });
  scroller.addEventListener('touchcancel', () => { touchY = -1; releasePull(); }, { passive: true });
}

// The entrance score (builds.css, Motion). A fresh render starts a cascade: --el 0, and --i on
// the fourteen rail items counted from ten above the current row (the cards carry their own
// --i from cardHTML). A rebuild inside the window (the live answer, a retry) resumes it:
// --el goes negative by the time elapsed, so the re-created rows and cards continue from where
// the old ones were. Outside the window a rebuild is a cut. A phone screen that is off stage
// does not play at all; pushView starts the page's entrance once it is on.
const CASCADE = 900;
function cascade(el, fresh, items) {
  const now = performance.now(), off = isPhone() && (el === list ? viewPushed : !viewPushed);
  if (fresh) el._t0 = now;
  const dt = now - (el._t0 || -CASCADE);
  if (off || (!fresh && dt > CASCADE)) { el.classList.remove('is-fresh'); return; }
  el.classList.add('is-fresh');
  el.style.setProperty('--el', (fresh ? 0 : -Math.round(dt)) + 'ms');
  if (!items) return;
  const c = items.findIndex((k) => k.getAttribute('aria-current') === 'true'), from = Math.max(0, c - 10);
  for (let i = from; i < Math.min(items.length, from + 14); i++) items[i].style.setProperty('--i', i - from);
}

function renderDetail(n, fresh) {
  const top = scroller.scrollTop;
  closeMenu();
  setHero(n);
  detail.innerHTML = detailHTML(n);   // also fills the header: the two are one season
  cascade(headin, fresh);             // eyebrow, title card, answer: their cues are in the CSS
  cascade(detail, fresh);             // the cards, on their own --i
  scroller.scrollTop = fresh ? 0 : top;
  // A size is a nicety; the open sequence is not. Anything thrown here would otherwise
  // strand the modal on "Checking…".
  try { probeRows(bySeason.get(n)); } catch (e) { /* the cards keep their catalogued sizes */ }
}

// The canvas is always the chosen season: a query filters the rail and nothing else, so the
// page on the right never jumps under a keystroke. fresh: a season change (fade the page in,
// scroll to top); otherwise an in-place rebuild that keeps the scroll position.
function renderCanvas(fresh) {
  if (season != null) renderDetail(season, fresh);
}

// A search hit lands on its card: scrolled into view, flashed once, and focused so the
// next Tab is the download link.
function spotlight(v) {
  const el = detail.querySelector('[data-v="' + CSS.escape(v) + '"]');
  if (!el) return;
  const box = el.closest('.bl-card') || el;
  // The card may still be parked on bl-arrive's backwards fill (--dy): centre the box where
  // it will land, not where the animation is holding it.
  const r = box.getBoundingClientRect(), s = scroller.getBoundingClientRect();
  const far = Math.abs(r.top - s.top) > innerHeight;
  const dy = new DOMMatrixReadOnly(getComputedStyle(box).transform).f;
  scroller.scrollTo({ top: scroller.scrollTop + (r.top + r.height / 2) - (s.top + s.height / 2) - dy, behavior: far ? 'auto' : glide() });
  for (const f of detail.querySelectorAll('.is-flash')) f.classList.remove('is-flash');
  // The ring is a pseudo-element the class creates (CSS .is-flash::after) and its decay runs
  // on its own; the class is set under reduced motion too (the ring is then simply on) and
  // a timer clears it either way.
  box.classList.add('is-flash');
  setTimeout(() => box.classList.remove('is-flash'), 1700);
  el.focus({ preventScroll: true });
}

/* ---------- The live join ---------- */

// One sentence per event (the live answer, a retry) and never the same sentence twice in a
// row: a re-render that changes nothing is not news.
function sayNow(msg) {
  if (!sayEl || sayEl.textContent === msg) return;
  sayEl.textContent = msg;
}

// Where focus is, in terms that survive a rebuild, and how to put it back.
function focusMemo() {
  const el = document.activeElement;
  if (!el || !dlg.contains(el)) return null;
  if (el.id === 'bl-title') return { title: true };
  if (el.dataset.k) return { k: el.dataset.k };
  if (el.classList.contains('bl-sel')) return { pick: el.closest('.bl-card').dataset.v };
  if (el.classList.contains('bl-srow')) return { row: el.dataset.season };
  if (el.dataset.v) return { card: el.dataset.v };
  return null;
}

function focusBack(m) {
  if (!m) return;
  let t = null;
  if (m.title) t = headin.querySelector('#bl-title');
  else if (m.k) t = detail.querySelector('[data-k="' + CSS.escape(m.k) + '"]');
  else if (m.card) t = detail.querySelector('[data-v="' + CSS.escape(m.card) + '"]');
  else if (m.pick) t = detail.querySelector('[data-v="' + CSS.escape(m.pick) + '"] .bl-sel');
  else if (m.row) t = list.querySelector('.bl-srow[data-season="' + m.row + '"]');
  if (t) t.focus({ preventScroll: true });
}

function rebuild() {
  // On the very first open liveState is always 'loading', so this rebuild always runs, and
  // on a card open the thing it destroys is the heading focusIn() just focused. The memo
  // carries it through.
  const memo = focusMemo(), top = list.scrollTop;
  renderSide();
  list.scrollTop = top;
  renderCanvas(false);
  focusBack(memo);
}

function applyLive(urls) {
  if (!dlg) return;
  // A zero-length array is treated as a failed check, not as "nothing exists". The mirror
  // having genuinely emptied and the mirror being unreachable are indistinguishable from
  // here, and only one of those two readings can be said out loud without lying.
  if (Array.isArray(urls) && urls.length) {
    liveSet = new Set();
    for (const u of urls) liveSet.add(u.slice(u.lastIndexOf('/') + 1));
    liveState = 'ok';
  } else {
    liveState = 'failed';
  }
  statsEpoch++;
  statsCache.clear();
  if (liveState === 'failed') showBanner(); else hideBanner();
  detail.classList.add('is-answer');   // the pills land as a ripple, 12ms apart down the grid (CSS)
  rebuild();
  setTimeout(() => detail.classList.remove('is-answer'), 600);
  sayNow((liveState === 'ok' ? 'Availability confirmed.'
    : 'Availability could not be checked. Builds are not shown until it can be.'));
}

function race(p) {
  const PENDING = Symbol();
  const late = new Promise((r) => setTimeout(() => r(PENDING), 400));
  Promise.race([p, late]).then(
    (v) => { if (v !== PENDING) applyLive(v); else p.then(applyLive, () => applyLive(null)); },
    () => applyLive(null)
  );
}

function showBanner() {
  if (banner) return;
  banner = document.createElement('div');
  banner.className = 'bl-banner';
  banner.innerHTML = '<span>Couldn’t reach the mirror, so the builds can’t be shown right now.</span>' +
    '<button class="btn btn-ghost bl-retry" type="button"><span>Try again</span></button>';
  dlg.classList.add('has-banner');
  placeBanner();
}

// The banner only ever lives on the phone's list screen, above the rows: the canvas is off
// stage and inert there, so the retry inside .bl-error would be unreachable. Everywhere else
// the error block on stage carries the retry, and the banner is simply removed.
function placeBanner() {
  if (!banner) return;
  if (!(isPhone() && !viewPushed)) { if (banner.parentNode) banner.remove(); return; }
  if (banner.parentNode !== side) side.insertBefore(banner, list);
}

function hideBanner() {
  if (!banner) return;
  banner.remove();
  banner = null;
  dlg.classList.remove('has-banner');
}

function retryLive() {
  if (!retryFn || liveState === 'loading') return;
  // hideBanner() below destroys the button that is being clicked, which drops focus to
  // <body> before applyLive ever runs, so its restore finds nothing. Note the intent here
  // and re-place focus on the heading once the page has been rebuilt; applyLive's memo
  // then carries it through the second rebuild.
  const act = document.activeElement;
  const hadRetry = !!(act && act.classList && act.classList.contains('bl-retry'));
  liveState = 'loading';
  statsEpoch++;
  statsCache.clear();
  hideBanner();
  rebuild();
  if (hadRetry) {
    const h = headin.querySelector('#bl-title');
    if (h) h.focus({ preventScroll: true });
  }
  sayNow('Checking the mirror again.');
  const p = retryFn();
  if (p && p.then) race(p); else applyLive(null);
}

/* ---------- Events ---------- */

function onClick(e) {
  const t = e.target.closest ? e.target : null;
  // A press anywhere but on the open menu or its own button closes the menu first.
  if (menu && !(t && t.closest('.bl-menu,button.bl-sel'))) closeMenu();
  if (e.target === dlg) { closeModal(false); return; }   // the backdrop
  if (!t) return;
  const pick = t.closest('button.bl-sel');
  if (pick) { openMenu(pick.closest('.bl-card')); return; }
  // A download: the arrow leaves with the file (CSS .is-sent); the click itself proceeds.
  const sent = (el) => { if (el && !el.classList.contains('is-sent')) { el.classList.add('is-sent'); el.addEventListener('animationend', () => el.classList.remove('is-sent'), { once: true }); } };
  const mi = t.closest('.bl-mi');
  if (mi) {
    // A link row downloads (the browser is already on its way) and the card turns to that
    // platform either way, so what was pressed is what the card now shows.
    const card = mi.closest('.bl-card'), p = mi.dataset.plat, link = mi.tagName === 'A';
    closeMenu();
    selectPlat(card, p);
    const b = card.querySelector('button.bl-sel');
    if (b) b.focus({ preventScroll: true });
    if (link) sent(card.querySelector('.bl-go'));
    return;
  }
  sent(t.closest('a.bl-go'));
  const el = t.closest('.bl-srow,.bl-close,.bl-back,.bl-retry');
  if (!el) return;
  if (el.classList.contains('bl-close')) return closeModal(false);
  if (el.classList.contains('bl-back')) return goBack();
  if (el.classList.contains('bl-retry')) return retryLive();
  selectSeason(+el.dataset.season, null);
  if (isPhone()) pushView();
}

const FOCUSABLE = 'a[href],button,input,select,textarea,[tabindex]';
function focusables() {
  const out = [];
  for (const el of dlg.querySelectorAll(FOCUSABLE))
    if (el.tabIndex >= 0 && !el.hidden && el.getClientRects().length && !el.closest('[inert]')) out.push(el);
  return out;
}

function onKey(e) {
  if (onMenuKey(e)) return;
  if (e.key === 'Escape') {
    e.preventDefault();   // the UA's close watcher would cut the exit short; closeModal owns the close
    closeModal(false);
    return;
  }
  if (e.key !== 'Tab') return;
  const f = focusables();
  if (!f.length) { e.preventDefault(); return; }
  const first = f[0], last = f[f.length - 1], at = document.activeElement;
  if (e.shiftKey && (at === first || at === dlg)) { e.preventDefault(); last.focus(); }
  else if (!e.shiftKey && at === last) { e.preventDefault(); first.focus(); }
}

// Roving tabindex over the rows: arrows and Home/End move focus; landing on a season row
// shows that season on the right, without pushing the phone stack; Enter does that.
function onListKey(e) {
  const items = [...list.querySelectorAll('[data-nav]')];
  const i = items.indexOf(document.activeElement);
  if (i < 0) return;
  const j = e.key === 'ArrowDown' ? i + 1 : e.key === 'ArrowUp' ? i - 1
    : e.key === 'Home' ? 0 : e.key === 'End' ? items.length - 1 : -1;
  if (j < 0 || j >= items.length) return;
  e.preventDefault();
  const to = items[j];
  if (to.classList.contains('bl-srow')) selectSeason(+to.dataset.season, null);
  for (const it of items) it.tabIndex = it === to ? 0 : -1;   // exactly one stop in the list
  to.focus();
}

/* ---------- Season selection, the phone stack, lock, close ---------- */

function selectSeason(n, v) {
  const fresh = n !== season;
  if (fresh && season != null) dlg.classList.remove('is-opening');   // a switch is a cut: the opening cues are over (CSS)
  season = n;
  markCurrent(n, !v);
  if (fresh) renderDetail(n, true);
  // replaceState, not pushState: Back should leave the modal, not walk 43 seasons.
  history.replaceState({ bl: 1, d: depth }, '', hashFor(n));
}

// Which screen is on stage on a phone; the other one is inert so Tab cannot reach it.
function syncStack() {
  const phone = isPhone();
  dlg.classList.toggle('is-pushed', viewPushed);
  canvas.inert = phone && !viewPushed;
  side.inert = phone && viewPushed;
  placeBanner();
  watchThumbs();
  landArt();
}

// The page slides in over the list and owns one history entry, so the browser's Back pops
// the page before it ever closes the dialog.
function pushView() {
  if (viewPushed) return;
  viewPushed = true;
  dlg.classList.remove('is-opening');   // a push is the reader's cut: the pushed cues apply (CSS)
  syncStack();
  // The page was rendered off stage without its entrance; it plays now, once the slide has
  // mostly landed (CSS --cue-*). A page that has already played simply slides back on.
  if (!detail.classList.contains('is-fresh')) { cascade(headin, true); cascade(detail, true); }
  if (!depth) { history.pushState({ bl: 1, d: 1 }, '', hashFor(season)); depth = 1; }
  const h = headin.querySelector('#bl-title');
  if (h) h.focus({ preventScroll: true });
}

function popView() {
  if (!viewPushed) return;
  viewPushed = false;
  syncStack();
  const r = list.querySelector('.bl-srow[aria-current="true"]');
  if (r) r.focus({ preventScroll: true });
}

function goBack() {
  if (depth) history.back();   // onPop does the visual pop
  else popView();
}

// A traversal that closes the dialog (Back, or the close's own history.go()) lands on a
// hash the page would otherwise reopen from, because its hashchange trails the popstate.
// isOpen() keeps saying yes until that hashchange has been ignored, or 250ms have passed
// for a traversal whose hash did not change and so fires none.
function shield() {
  suppress = true;
  clearTimeout(shieldT);
  shieldT = setTimeout(() => { suppress = false; }, 250);
}

// Where the reader was before the modal opened, put back instantly (see open()).
function settle() {
  if (place) scrollTo({ left: place[0], top: place[1], behavior: 'instant' });
}

// #season-N and #builds are the modal's own; anything else in the hash belongs to the page.
const OWN_HASH = /^#(?:builds|season-(?:\d+|pre))$/;
function stripHash() {
  if (OWN_HASH.test(location.hash)) history.replaceState(history.state, '', location.pathname + location.search);
}

function onPop() {
  if (!dlg) return;
  if (!dlg.open) {
    if (suppress) shield();
    if (stripOwed) {
      stripOwed = false;
      stripHash();
      try { history.scrollRestoration = restore; } catch (e) { /* see open() */ }
      // The place is put back now, and again once the anchor's id is back (the traversal's own
      // scroll-to-fragment runs at the next lifecycle update, past any rAF of this frame).
      settle();
      requestAnimationFrame(() => requestAnimationFrame(() => {
        if (hid) { try { hid.id = decodeURIComponent(fromId); } catch (e) { hid.id = fromId; } hid = null; }
        settle();
        place = null;
      }));
    }
    return;
  }
  if (depth) { depth = 0; popView(); return; }
  shield();
  closeModal(true);
}

function lock() {
  const sbw = innerWidth - document.documentElement.clientWidth;
  document.documentElement.style.setProperty('--bl-sbw', sbw + 'px');
  document.documentElement.classList.add('bl-lock');
}

function unlock() {
  document.documentElement.classList.remove('bl-lock');
  document.documentElement.style.removeProperty('--bl-sbw');
}

// The curtain comes down first (CSS .is-closing, one dissolve) and the dialog closes once it
// has gone. A second request inside that window is the same request; a Back arriving then
// still records itself, so onClose takes the right history branch. Reduced motion closes now.
function closeModal(fromPop) {
  if (!dlg || !dlg.open) return;
  popping = popping || !!fromPop;
  if (dlg.classList.contains('is-closing')) return;
  if (reduceMotion()) { dlg.close(); return; }
  dlg.classList.add('is-closing');
  closeMenu();
  setTimeout(() => { if (dlg.open) dlg.close(); }, 340);   // --t-move and a frame
}

function onClose() {
  unlock();
  // The score is reset, so the next open is a first frame again: the rail cascades, the wash
  // dips in, and no exit or opening class survives.
  dlg.classList.remove('is-closing', 'is-opening');
  delete list.dataset.view;
  railHTML = '';   // a reopen always paints once
  heroN = null; hero.className = 'bl-hero no-art'; hero.innerHTML = '';
  closeMenu();
  // history.go() re-enters here through popstate, so the flags are cleared before the call.
  const owed = pushed && !popping;
  const steps = depth + 1;
  pushed = false;
  popping = false;
  depth = 0;
  viewPushed = false;
  // isOpen() stays true until the traversal's popstate arrives and shield() has seen the
  // hashchange out; otherwise landing on a #season-N entry would reopen what just closed.
  // The address bar never keeps a season the reader has closed. A traversal lands where it
  // lands and the hash is dropped once it has (onPop); a Back that did the closing has
  // already landed, so it is dropped here.
  if (owed) {
    // Re-entering an entry whose fragment names an element scrolls to it (smoothly, over a
    // second, after the popstate) whatever scrollRestoration says. With no element to find
    // there is nothing to scroll to; the id comes back two frames later (onPop).
    let el = null;
    try { el = fromId && document.getElementById(decodeURIComponent(fromId)); } catch (e) { /* not an id */ }
    if (el) { hid = el; el.removeAttribute('id'); }
    suppress = true; stripOwed = true; history.go(-steps);
  }
  else { stripHash(); try { history.scrollRestoration = restore; } catch (e) { /* see open() */ } place = null; }
  if (!opener) return;
  const o = opener;
  opener = null;
  o.focus({ preventScroll: true });         // preventScroll: focusing a .film inside a
  const r = o.getBoundingClientRect();      // horizontal .reel would yank the reel sideways
  if (r.bottom < 0 || r.top > innerHeight || r.right < 0 || r.left > innerWidth)
    o.scrollIntoView({ block: 'nearest', inline: 'center', behavior: glide() });
}

function focusIn(ctx) {
  const desktop = matchMedia('(hover: hover) and (pointer: fine)').matches;
  // Autofocusing the field on touch raises the keyboard over the content before the user
  // has seen a single row. And when a card was tapped, the first thing announced should be
  // the season they picked, not "search".
  let t;
  if (isPhone() && !viewPushed) t = list.querySelector('.bl-srow[aria-current="true"]') || q;
  else if (ctx.season != null) t = headin.querySelector('#bl-title') || q;
  else t = desktop ? q : (headin.querySelector('#bl-title') || q);
  t.focus({ preventScroll: true });
}

/* ---------- The five HEADs, and one preconnect ---------- */

function setSize(k, text) {
  sizeText.set(k, text);
  const a = detail.querySelector('[data-k="' + CSS.escape(k) + '"]');
  if (!a) return;
  // The size is no longer printed on the button; it rides on its tooltip and its name.
  a.title = text;
  const label = a.getAttribute('aria-label') || '';
  const spoken = / [GM]B$/.test(text) ? ', ' + text.replace(' GB', ' gigabytes').replace(' MB', ' megabytes') : '';
  a.setAttribute('aria-label', label.replace(/, [\d.]+ (gigabytes|megabytes)/, '').replace(/\.$/, '') + spoken + '.');
}

// No Range requests, ever: this domain answers a first Range against a cold object with a
// 200 and the full body, which on a 44 GB file would be catastrophic. HEAD carries no
// custom headers, so it stays CORS-simple and skips the preflight.
function probeRows(rows) {
  for (const i of rows) {
    if (statusOf(i) !== 'available' || D.B.b[i]) continue;
    const k = keyOf(i);
    if (probed.has(k)) continue;
    probed.add(k);
    setSize(k, '···');
    // Safari 15.4-15.6 has showModal and not AbortSignal.timeout, and the page's entry gate
    // only tests showModal, the same guard the bootstrap already uses for its own fetch.
    fetch(BASE + encodeURIComponent(k), AbortSignal.timeout
      ? { method: 'HEAD', signal: AbortSignal.timeout(4000) } : { method: 'HEAD' })
      .then((r) => {
        const b = +r.headers.get('content-length');
        setSize(k, b ? fmtSize(Math.round(b / 1048576)) : 'size unknown');
      })
      // The link stays either way: existence is confirmed independently of size.
      .catch(() => setSize(k, 'size unknown'));
  }
}

function preconnect() {
  if (warmed) return;
  warmed = true;
  const l = document.createElement('link');
  l.rel = 'preconnect';
  l.href = BASE;
  l.crossOrigin = '';
  document.head.appendChild(l);   // on first open, never in <head>
}

/* ---------- Exports ---------- */

export function isOpen() { return !!(dlg && (dlg.open || suppress)); }

export function open(ctx) {
  if (!D) { decode(ctx.data); buildShell(ctx.styled); }
  if (ctx.retry) retryFn = ctx.retry;
  const want = ctx.season != null && bySeason.has(ctx.season) ? ctx.season : firstSeason();

  if (dlg.open) {
    selectSeason(want, null);
    if (isPhone() && ctx.season != null) pushView();
    return;
  }
  opener = ctx.opener || null;
  clearTimeout(shieldT);
  suppress = false;

  // Back must close the modal, including for someone who arrived on a shared link, so a
  // load-time open pushes its own entry rather than leaving Back pointed at the referrer.
  // The entry being left may carry the page's own fragment (#faq, #seasons, …). The close
  // traverses back to it, and the browser would re-apply that fragment: a scroll of up to
  // 3 000px after the dialog has gone, undoing the poster restore in onClose. Mark the entry
  // manual (the pushed entry inherits it) and remember the place; onPop puts both back.
  place = [scrollX, scrollY];
  fromId = OWN_HASH.test(location.hash) ? '' : location.hash.slice(1);
  restore = history.scrollRestoration;
  try { history.scrollRestoration = 'manual'; } catch (e) { /* not supported: the jump stays */ }
  history.pushState({ bl: 1, d: 0 }, '', hashFor(want));
  pushed = true;
  depth = 0;
  // A season was asked for: on a phone the page is already on stage, and Back leaves the
  // dialog. Only the bare #builds entry starts on the list.
  viewPushed = isPhone() && ctx.season != null;

  // Before lock(), because renderDetail() below dispatches the HEAD probes to this same
  // origin in this same task: a preconnect issued after them warms nothing.
  preconnect();
  lock();
  // Before showModal, whose focusing steps compute style: a deep-linked page is on stage from
  // the first frame and never slides in. The opening cues are armed, and the shell grows from
  // the poster that was tapped: its centre in viewport px, less the dialog's own offset from
  // the viewport's centre (CSS bl-in); a shared link has no poster and grows from the middle.
  dlg.classList.toggle('is-pushed', viewPushed);
  dlg.classList.add('is-opening');
  const o = opener && opener.getBoundingClientRect();
  dlg.style.transformOrigin = o ? 'calc(' + (o.x + o.width / 2) + 'px - 50vw + 50%) calc(' + (o.y + o.height / 2) + 'px - 50dvh + 50%)' : '';
  dlg.showModal();
  // A reopen after a failure asks again below (retryLive); the banner only stays put when
  // there is nothing to ask with.
  if (liveState === 'failed' && !retryFn) showBanner();
  season = want;
  renderSide();
  // Instant, not smooth: a pan across rows nobody has seen yet. renderSide has already marked
  // the row current, and the rail's cascade is counted from it.
  const cur = list.querySelector('.bl-srow[aria-current="true"]');
  if (cur) cur.scrollIntoView({ block: 'nearest' });
  renderDetail(want, true);
  syncStack();
  dlg.setAttribute('aria-labelledby', 'bl-title');
  focusIn(ctx);

  if (liveState === 'loading' && ctx.live) race(ctx.live);
  else if (liveState === 'failed' && retryFn) retryLive();   // a blip should not be sticky for the session
}
