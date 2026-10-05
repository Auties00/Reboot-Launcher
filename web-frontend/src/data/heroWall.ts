export interface WallColumn {
  // Whole seconds: the stepped rise depends on it.
  duration: number;
  seasons: number[];
}

export const WALL_REPEAT = 3;

export const WALL_COLUMNS: readonly WallColumn[] = [
  { duration: 46, seasons: [1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41] },
  { duration: 62, seasons: [2, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42] },
  { duration: 54, seasons: [3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 5] },
  { duration: 70, seasons: [4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 11] },
];

export interface Spark {
  x: number;
  duration: number;
  delay: number;
  size: number;
}

export const SPARKS: readonly Spark[] = [
  { x: 12, duration: 9, delay: 0, size: 3 },
  { x: 22, duration: 12, delay: 2, size: 2 },
  { x: 31, duration: 10, delay: 5, size: 3 },
  { x: 44, duration: 14, delay: 1, size: 2 },
  { x: 55, duration: 11, delay: 4, size: 3 },
  { x: 63, duration: 9, delay: 7, size: 2 },
  { x: 72, duration: 13, delay: 3, size: 3 },
  { x: 81, duration: 10, delay: 6, size: 2 },
  { x: 88, duration: 12, delay: 2, size: 3 },
  { x: 38, duration: 15, delay: 8, size: 2 },
  { x: 50, duration: 8, delay: 9, size: 2 },
  { x: 68, duration: 11, delay: 11, size: 3 },
];
