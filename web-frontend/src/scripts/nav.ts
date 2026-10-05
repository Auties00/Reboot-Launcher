export function initNavActions(): void {
  const actions = document.getElementById('nav-actions');
  const inner = actions?.querySelector<HTMLElement>('.nav-actions-inner');
  const heroCta = document.getElementById('hero-cta');
  if (!actions || !inner || !heroCta || !('IntersectionObserver' in window)) return;

  const measure = () => actions.style.setProperty('--actions-w', inner.offsetWidth + 'px');
  // Measured after layout rather than forcing one.
  if ('ResizeObserver' in window) new ResizeObserver(measure).observe(inner);
  else measure();

  requestAnimationFrame(() => requestAnimationFrame(() => actions.classList.remove('no-anim')));

  new IntersectionObserver(([entry]) => {
    actions.classList.toggle('is-hidden', entry.isIntersecting);
  }, { rootMargin: '-64px 0px 0px 0px', threshold: 0 }).observe(heroCta);
}

export function initNavSpy(): void {
  const links = [...document.querySelectorAll<HTMLAnchorElement>('.nav-links a[href^="#"]')];
  if (!links.length || !('IntersectionObserver' in window)) return;
  const sections = new Map<Element, HTMLAnchorElement>();
  for (const link of links) {
    const target = document.querySelector(link.getAttribute('href')!);
    if (target) sections.set(target, link);
  }
  if (!sections.size) return;

  const seen = new Set<Element>();
  const io = new IntersectionObserver((entries) => {
    for (const e of entries) {
      if (e.isIntersecting) seen.add(e.target);
      else seen.delete(e.target);
    }
    for (const link of links) link.classList.remove('is-current');
    for (const [section, link] of sections) {
      if (seen.has(section)) { link.classList.add('is-current'); break; }
    }
  }, { rootMargin: '-45% 0px -50% 0px' });
  for (const section of sections.keys()) io.observe(section);
}
