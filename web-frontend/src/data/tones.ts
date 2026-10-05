// No imports: the modal's lazy chunk includes this module.
export type Tone = 'stable' | 'partial' | 'unstable';

export const TONE_LABEL: Record<Tone, string> = {
  stable: 'Plays great',
  partial: 'Works, with a few rough edges',
  unstable: 'Experimental, expect bugs',
};

export const TONE_SPOKEN: Record<Tone, string> = {
  stable: 'plays great',
  partial: 'works with a few rough edges',
  unstable: 'experimental',
};

const PARTIAL = new Set([1, 2, 16, 17, 18, 19]);
const STABLE_FROM = 3, STABLE_TO = 15;

export function toneOf(season: number): Tone {
  if (PARTIAL.has(season)) return 'partial';
  if (season >= STABLE_FROM && season <= STABLE_TO) return 'stable';
  return 'unstable';
}
