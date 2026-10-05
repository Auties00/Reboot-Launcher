import type { Availability } from './availability';
import type { Catalog } from './catalog';

export const MIRROR = 'https://builds.rebootfn.org/';

export const downloadUrl = (key: string): string => MIRROR + encodeURIComponent(key);

function fmtSize(mib: number): string | null {
  if (!mib) return null;
  const gb = (mib * 1048576) / 1e9;
  if (gb >= 100) return Math.round(gb) + ' GB';
  if (gb >= 10) return gb.toFixed(1) + ' GB';
  if (gb >= 1) return gb.toFixed(2) + ' GB';
  return Math.round((mib * 1048576) / 1e6) + ' MB';
}

export class Sizes {
  private readonly text = new Map<string, string>();
  private readonly probed = new Set<string>();

  constructor(
    private readonly cat: Catalog,
    private readonly live: Availability,
    private readonly onChange: (key: string, text: string) => void,
  ) {}

  of(i: number): string {
    const k = this.cat.keyOf(i);
    return this.text.get(k) ?? (fmtSize(this.cat.sizeOf(i)) || '');
  }

  private set(k: string, text: string): void {
    this.text.set(k, text);
    this.onChange(k, text);
  }

  // HEAD, never Range: a cold Range on this bucket answers with the full body.
  probe(rows: number[]): void {
    for (const i of rows) {
      if (this.live.statusOf(i) !== 'available' || this.cat.sizeOf(i)) continue;
      const k = this.cat.keyOf(i);
      if (this.probed.has(k)) continue;
      this.probed.add(k);
      this.set(k, '···');
      const init: RequestInit = { method: 'HEAD' };
      if (AbortSignal.timeout) init.signal = AbortSignal.timeout(4000);
      fetch(downloadUrl(k), init)
        .then((r) => {
          const b = +(r.headers.get('content-length') ?? 0);
          this.set(k, fmtSize(Math.round(b / 1048576)) ?? 'size unknown');
        })
        .catch(() => this.set(k, 'size unknown'));
    }
  }
}
