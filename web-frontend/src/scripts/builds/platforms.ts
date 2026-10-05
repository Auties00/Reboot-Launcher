export const PLATS = ['windows', 'mac', 'ios', 'android', 'switch', 'ps4', 'ps5', 'xbox_one', 'xbox_series'] as const;
export type Platform = (typeof PLATS)[number];

export const PLAT_NAME: Record<Platform, string> = {
  windows: 'Windows', mac: 'macOS', ios: 'iOS', android: 'Android', switch: 'Switch',
  ps4: 'PS4', ps5: 'PS5', xbox_one: 'Xbox One', xbox_series: 'Xbox Series',
};

// Mac support ended before Apple Silicon; Windows on Arm runs the x64 client, so there is no Arm build.
export const ARCH: Partial<Record<Platform, string>> = { windows: 'x86', mac: 'x86' };
export const ARM_NOTE = 'also playable on Windows on Arm from 38.00: the x64 client under Prism emulation, with a native Easy Anti-Cheat; no separate Arm package exists';

export const MINE: Platform = (() => {
  const nav = navigator as Navigator & { userAgentData?: { platform?: string } };
  const pf = nav.userAgentData?.platform || navigator.platform || '', ua = navigator.userAgent || '';
  if (/android/i.test(pf) || /android/i.test(ua)) return 'android';
  if (/iphone|ipad|ipod/i.test(pf + ua) || (/mac/i.test(pf) && navigator.maxTouchPoints > 1)) return 'ios';
  if (/mac/i.test(pf)) return 'mac';
  return 'windows';
})();
