import type { Catalog, Version } from './catalog';
import { plural } from './util';

export type LiveState = 'loading' | 'ok' | 'failed';

export type Status = 'available' | 'unavailable' | 'missing' | 'unknown';

interface SeasonStats {
  versions: number;
  dl: number;
  allLost: number;
}

// Only the mirror's own index can say "available"; a failed check means "unknown", never "unavailable".
export class Availability {
  state: LiveState = 'loading';
  private live = new Set<string>();
  private stats = new Map<number, SeasonStats>();

  constructor(private readonly cat: Catalog) {}

  apply(urls: unknown): void {
    const names = Array.isArray(urls) ? urls.filter((u): u is string => typeof u === 'string') : [];
    // An empty list is a failed check: unreachable and empty look the same from here.
    if (names.length) {
      this.live = new Set(names.map((u) => u.slice(u.lastIndexOf('/') + 1)));
      this.state = 'ok';
    } else {
      this.state = 'failed';
    }
    this.stats.clear();
  }

  reset(): void {
    this.state = 'loading';
    this.stats.clear();
  }

  facetStatus(has: boolean, i: number | undefined): Status {
    if (this.state === 'ok' && i != null && this.live.has(this.cat.keyOf(i))) return 'available';
    if (!has) return 'missing';
    if (this.state !== 'ok') return 'unknown';
    return 'unavailable';
  }

  statusOf(i: number): Status {
    return this.facetStatus(this.cat.hasCopy(i), i);
  }

  fateOf(g: Version): Status {
    let unknown = false, unavailable = false;
    for (const f of g.x) {
      const s = this.facetStatus(f.has, g.b[f.p]);
      if (s === 'available') return 'available';
      if (s === 'unknown') unknown = true;
      else if (s === 'unavailable') unavailable = true;
    }
    return unknown ? 'unknown' : unavailable ? 'unavailable' : 'missing';
  }

  seasonStats(n: number): SeasonStats {
    let s = this.stats.get(n);
    if (s) return s;
    const vs = this.cat.versions.get(n)!;
    s = { versions: vs.length, dl: 0, allLost: 0 };
    for (const g of vs) {
      let anyAvail = false, anyLive = false;
      for (const f of g.x) {
        const st = this.facetStatus(f.has, g.b[f.p]);
        if (st === 'available') anyAvail = true;
        if (st !== 'missing') anyLive = true;
      }
      if (anyAvail) s.dl++;
      if (!anyLive) s.allLost++;
    }
    this.stats.set(n, s);
    return s;
  }

  isDead(n: number): boolean {
    return this.state === 'ok' && !this.seasonStats(n).dl;
  }

  rowFact(n: number): string {
    const st = this.seasonStats(n);
    if (this.state !== 'ok') return plural(st.versions, 'version');
    if (st.dl) return st.dl + ' to play';
    if (st.allLost === st.versions) return 'all lost';
    return 'nothing mirrored';
  }

  rowAria(n: number): string {
    return this.cat.rowWho(n) + '. ' + this.rowFact(n) + (this.state === 'ok' ? '.' : '. Availability unverified.');
  }
}
