const ESCAPES: Record<string, string> = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

export const esc = (s: unknown): string => String(s).replace(/[&<>"']/g, (c) => ESCAPES[c]);

export const pad2 = (n: number): string => (n < 10 ? '0' + n : '' + n);

export const plural = (n: number, word: string): string => n + ' ' + word + (n === 1 ? '' : 's');

const reduceMQ = matchMedia('(prefers-reduced-motion: reduce)');
export const reduceMotion = (): boolean => reduceMQ.matches;

export const glide = (): ScrollBehavior => (reduceMotion() ? 'auto' : 'smooth');

const phoneMQ = matchMedia('(max-width: 760px)');
export const isPhone = (): boolean => phoneMQ.matches;
export const onPhoneChange = (fn: () => void): void => phoneMQ.addEventListener('change', fn);

export const spokenSize = (size: string): string =>
  / [GM]B$/.test(size) ? ', ' + size.replace(' GB', ' gigabytes').replace(' MB', ' megabytes') : '';
