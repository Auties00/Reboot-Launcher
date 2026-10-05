import catalogueJson from './builds.json';
import type { Catalogue } from './catalogue';
import { TONE_SPOKEN, toneOf, type Tone } from './tones';

const catalogue = catalogueJson as Catalogue;

export interface Film {
  season: number;
  href: string;
  prefix: string;
  tag: string;
  title: string;
  label: string;
  tone: Tone;
  art: string;
}

export interface Reel {
  chapter: number;
  heading: string;
  prevLabel: string;
  nextLabel: string;
  reelLabel: string;
  films: Film[];
}

const INTERNAL = 99;
const pad2 = (n: number) => String(n).padStart(2, '0');

const BUCKETS: Film[] = [
  { season: 0, href: '#season-pre', prefix: '', tag: 'OT', title: 'Online Tests', label: 'Online Tests, alpha builds', tone: toneOf(0), art: 'seasons/s00' },
  { season: INTERNAL, href: `#season-${INTERNAL}`, prefix: '', tag: 'DEV', title: 'Internal builds', label: 'Internal builds, dev builds', tone: toneOf(INTERNAL), art: `seasons/s${INTERNAL}` },
];

function seasonFilm(season: number): Film {
  const info = catalogue.S[season];
  // Season X only ever went by its letter.
  const tag = season === 10 ? 'X' : String(season);
  return {
    season,
    href: `#season-${season}`,
    prefix: 'S',
    tag,
    title: info.name,
    label: `Season ${tag}, ${info.name}`,
    tone: toneOf(season),
    art: `seasons/s${pad2(season)}`,
  };
}

function buildReels(): Reel[] {
  const chapters = new Map<number, Film[]>();
  const seasons = Object.keys(catalogue.S).map(Number).filter((n) => n !== INTERNAL).sort((a, b) => a - b);
  for (const season of seasons) {
    const ch = catalogue.S[season].ch;
    if (!chapters.has(ch)) chapters.set(ch, []);
    chapters.get(ch)!.push(seasonFilm(season));
  }
  const reels: Reel[] = [{
    chapter: 0,
    heading: 'Outside the chapters',
    prevLabel: 'Previous, Outside the chapters',
    nextLabel: 'Next, Outside the chapters',
    reelLabel: 'Builds outside the chapters, scroll sideways',
    films: BUCKETS,
  }];
  for (const [chapter, films] of [...chapters].sort(([a], [b]) => a - b)) {
    reels.push({
      chapter,
      heading: `Chapter ${chapter}`,
      prevLabel: `Previous seasons, Chapter ${chapter}`,
      nextLabel: `Next seasons, Chapter ${chapter}`,
      reelLabel: `Chapter ${chapter} seasons, scroll sideways`,
      films,
    });
  }
  return reels;
}

export const REELS: readonly Reel[] = buildReels();

export const filmAria = (film: Film): string => `${film.label}, ${TONE_SPOKEN[film.tone]}. See its builds`;
