export interface SeasonInfo {
  name: string;
  ch: number;
  label: string;
  date: string;
}

export interface BuildColumns {
  v: string[];
  p: number[];
  r: number[];
  s: number[];
  e: number[];
  w: number[];
  m: number[];
  b: number[];
  ko: Record<string, string>;
  a: string[];
  c: string[];
}

export interface Catalogue {
  g: string;
  P: string[];
  R: string[];
  W: (string | null)[];
  E: string[];
  S: Record<string, SeasonInfo>;
  B: BuildColumns;
  X?: string[];
  V?: Record<string, string>;
  C?: Record<string, Record<string, string>>;
}
