export const SITE = {
  title: 'Project Reboot',
  description: 'Play the old Fortnite seasons again with your friends. Free, fan-made, and available on Windows, Linux and macOS.',
  ogDescription: 'Play the old Fortnite seasons again with your friends. Free, on Windows, Linux and macOS.',
  themeColor: '#06111C',
} as const;

export const LINKS = {
  releases: 'https://github.com/Auties00/Reboot-Launcher/releases',
  latestRelease: 'https://github.com/Auties00/Reboot-Launcher/releases/latest',
  discord: 'https://discord.gg/rebootmp',
  launcherIssues: 'https://github.com/Auties00/Reboot-Launcher/issues',
  gameServerIssues: 'https://github.com/Milxnor/Project-Reboot-3.0/issues',
} as const;

export interface Repo {
  owner: string;
  name: string;
  title: string;
  footer: string;
  blurb: string;
}

export const REPOS: readonly Repo[] = [
  {
    owner: 'Auties00',
    name: 'Reboot-Launcher',
    title: 'The launcher',
    footer: 'The launcher',
    blurb: 'The app you download. It looks after your seasons, runs your lobbies, and connects you to your friends.',
  },
  {
    owner: 'Milxnor',
    name: 'Project-Reboot-3.0',
    title: 'The game server',
    footer: 'The game engine',
    blurb: 'The engine that brings an old season back to life and runs the match. Made for Seasons 3 to 15, with more on the way.',
  },
  {
    owner: 'Lawin0129',
    name: 'LawinServer',
    title: 'The backend',
    footer: 'The backend',
    blurb: "LawinServer V1. Stands in for Epic's servers so the game can log you in, load your locker, and get you to the lobby without an account.",
  },
  {
    owner: 'Lawin0129',
    name: 'FortMatchmaker',
    title: 'The matchmaker',
    footer: 'The matchmaker',
    blurb: 'The Lawin matchmaker. When you press play, it is what puts you and your friends into the same match.',
  },
];

export const repoUrl = (repo: Repo): string => `https://github.com/${repo.owner}/${repo.name}`;

export const NAV_LINKS = [
  { href: '#features', label: 'Features' },
  { href: '#how', label: 'How to play' },
  { href: '#seasons', label: 'Seasons' },
  { href: '#code', label: 'Source' },
  { href: '#faq', label: 'FAQ' },
] as const;
