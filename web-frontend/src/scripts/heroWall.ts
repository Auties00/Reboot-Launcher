export function initHeroWall(): void {
  const visual = document.getElementById('hero-visual');
  const wall = document.getElementById('wall');
  if (!visual || !wall) return;
  const hero = visual.closest<HTMLElement>('.hero') ?? visual;
  const root = document.documentElement;

  const eager = () => { for (const img of wall.querySelectorAll('img')) img.loading = 'eager'; };
  if (!root.classList.contains('bl-deep')) eager();
  else {
    // `close` does not bubble; a capturing listener still sees it.
    document.addEventListener('close', (e) => {
      if ((e.target as Element | null)?.id === 'bl-dialog') { root.classList.remove('bl-deep'); eager(); }
    }, true);
  }

  const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (!reduce && matchMedia('(pointer: fine)').matches) {
    hero.addEventListener('mousemove', (e) => {
      const r = hero.getBoundingClientRect();
      const px = (e.clientX - r.left) / r.width - 0.5;
      const py = (e.clientY - r.top) / r.height - 0.5;
      wall.style.setProperty('--ty', px * 8 + 'deg');
      wall.style.setProperty('--tx', -py * 6 + 'deg');
    });
    hero.addEventListener('mouseleave', () => {
      wall.style.setProperty('--ty', '0deg');
      wall.style.setProperty('--tx', '0deg');
    });
  }

  if ('IntersectionObserver' in window) {
    new IntersectionObserver(([entry]) => {
      visual.classList.toggle('is-idle', !entry.isIntersecting);
    }, { threshold: 0.05 }).observe(visual);
  }
}
