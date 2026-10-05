import type { Catalogue } from '../../data/catalogue';
import { PLATS, type Platform } from './platforms';
import { esc, pad2 } from './util';

export const INTERNAL = 99;

interface Bucket {
  name: string;
  sub: string;
  eye: string;
}

const BUCKET: Record<number, Bucket> = {
  0: { name: 'Online Tests', sub: 'Alpha builds', eye: 'Alpha <span>·</span> Online Tests <span>·</span> Dec 2014 – Jul 2017' },
  [INTERNAL]: { name: 'Internal builds', sub: 'Dev builds', eye: 'Internal <span>·</span> dev and certification builds <span>·</span> 2017 – 2022' },
};

export interface Facet {
  p: Platform;
  has: boolean;
  how: 'source' | 'timeline';
  cl: string | null;
  arm: boolean;
}

export interface Version {
  v: string;
  b: Partial<Record<Platform, number>>;
  idx: number[];
  x: Facet[];
}

export const art = (n: number, size = ''): string => `/seasons/s${pad2(n)}${size}.webp`;

export class Catalog {
  readonly bySeason = new Map<number, number[]>([[0, []]]);
  readonly versions = new Map<number, Version[]>();
  readonly order: number[] = [0];
  private readonly X: string[];

  constructor(readonly D: Catalogue) {
    const B = D.B;
    this.X = D.X ?? [];
    let max = 0;
    for (const k in D.S) if (+k > max) max = +k;
    if (D.S[INTERNAL]) { this.bySeason.set(INTERNAL, []); this.order.push(INTERNAL); }
    for (let s = 1; s <= max; s++) {
      if (D.S[s] && s !== INTERNAL) { this.bySeason.set(s, []); this.order.push(s); }
    }
    for (let i = 0; i < B.v.length; i++) this.bySeason.get(B.s[i])!.push(i);

    for (const [s, idx] of this.bySeason) {
      const out: Version[] = [], at = new Map<string, Version>();
      for (const i of idx) {
        let g = at.get(B.v[i]);
        if (!g) { g = { v: B.v[i], b: {}, idx: [], x: [] }; at.set(B.v[i], g); out.push(g); }
        g.b[D.P[B.p[i]] as Platform] = i;
        g.idx.push(i);
      }
      for (const g of out) g.x = this.facetsOf(g.v, g.b);
      this.versions.set(s, out);
    }
  }

  // Most keys are {platform}-{version}.{ext}; the rest carry an explicit stem in ko.
  keyOf(i: number): string {
    const B = this.D.B;
    return (B.ko[i] ?? `${this.D.P[B.p[i]]}-${B.v[i]}`) + '.' + this.D.E[B.e[i]];
  }

  platformOf(i: number): Platform {
    return this.D.P[this.D.B.p[i]] as Platform;
  }

  // V: a letter per X platform. '.' never existed, A/a copy known, M/m none; uppercase when a source names it.
  private facetsOf(v: string, rows: Version['b']): Facet[] {
    const { D, X } = this;
    const code = D.V?.[v], out: Facet[] = [];
    const armAt = X.indexOf('windows_arm');
    for (const p of PLATS) {
      let c = code ? code[X.indexOf(p)] : undefined;
      const row = rows[p];
      if (!code && row != null) c = D.R[D.B.r[row]] === 'missing' ? 'M' : 'A';
      if (!c || c === '.') continue;
      const cl = D.C?.[v]?.[X.indexOf(p)];
      const arm = p === 'windows' && !!code && armAt >= 0 && code[armAt] !== '.';
      out.push({ p, has: c === 'a' || c === 'A', how: c === c.toUpperCase() ? 'source' : 'timeline', cl: cl || null, arm });
    }
    return out;
  }

  hasCopy(i: number): boolean {
    const { D } = this;
    const c = D.V?.[D.B.v[i]]?.[this.X.indexOf(D.P[D.B.p[i]])];
    return c ? c === 'a' || c === 'A' : D.R[D.B.r[i]] !== 'missing';
  }

  whyMissing(i: number): string {
    return this.D.W[this.D.B.w[i]] ?? '';
  }

  sizeOf(i: number): number {
    return this.D.B.b[i];
  }

  clOf(g: Version): string {
    const c = this.D.B.c[g.idx[0]];
    return c ? c.split(' ')[0] : '';
  }

  clFor(g: Version, p: Platform): string {
    return this.D.C?.[g.v]?.[this.X.indexOf(p)] || this.clOf(g);
  }

  group(v: string): Version | null {
    for (const list of this.versions.values()) for (const g of list) if (g.v === v) return g;
    return null;
  }

  has(n: number): boolean {
    return this.bySeason.has(n);
  }

  firstSeason(): number {
    return this.order.find((n) => !BUCKET[n]) ?? this.order[0];
  }

  name(n: number): string {
    return BUCKET[n] ? BUCKET[n].name : this.D.S[n].name;
  }

  chapter(n: number): number {
    return BUCKET[n] ? 0 : this.D.S[n].ch;
  }

  chapterName(ch: number): string {
    return ch ? 'Chapter ' + ch : 'Outside the chapters';
  }

  chapterYears(ch: number): string {
    if (!ch) return '';
    let lo = 0, hi = 0;
    for (const s of this.order) {
      if (!s || this.chapter(s) !== ch) continue;
      const m = /\d{4}/.exec(this.D.S[s].date);
      if (!m) continue;
      const y = +m[0];
      if (!lo || y < lo) lo = y;
      if (y > hi) hi = y;
    }
    return !lo ? '' : lo === hi ? '' + lo : lo + '–' + hi;
  }

  rowTitle(n: number): string {
    return BUCKET[n] ? BUCKET[n].name : this.D.S[n].label;
  }

  rowSub(n: number): string {
    if (BUCKET[n]) return BUCKET[n].sub;
    const s = this.D.S[n];
    return s.name === s.label ? '' : s.name;
  }

  rowWho(n: number): string {
    if (!n) return 'Pre-release and Open Test builds';
    const s = this.D.S[n];
    return s.label === s.name ? s.label : s.name + ', Chapter ' + s.ch + ' ' + s.label;
  }

  eyebrow(n: number): string {
    if (BUCKET[n]) return BUCKET[n].eye;
    const s = this.D.S[n];
    return 'Chapter ' + s.ch + ' <span>·</span> ' + esc(s.label) + ' <span>·</span> ' + esc(s.date);
  }
}
