const STEP_RATE = 60;

export const stepsFor = (seconds: number): number => Math.round(seconds * STEP_RATE);

const round = (n: number) => Number(n.toFixed(4));

// Sampled at sixtieths, so steps(<whole seconds>) advances once per 1/60 s (var() in keyframe timing functions is ignored).
export function sparkRiseKeyframes(name: string): string {
  const opacity = (p: number) => Math.max(0, Math.min(0.9 * p / 0.12, 0.9 - 0.4 * (p - 0.12) / 0.58, 0.5 * (1 - p) / 0.3));
  let frames = '';
  for (let k = 0; k <= STEP_RATE; k++) {
    const p = k / STEP_RATE;
    frames += `${round(p * 100)}%{transform:translateY(${round(-560 * p)}px) scale(${round(0.6 + 0.5 * p)});opacity:${round(opacity(p))}}`;
  }
  return `@keyframes ${name}{${frames}}`;
}
