// Observer-gated: loading="lazy" alone fetches ~1250px ahead, even under the phone's covering page.
export class RailThumbs {
  private io: IntersectionObserver | null = null;
  private idle = 0;

  constructor(private readonly list: HTMLElement, private readonly side: HTMLElement) {}

  watch(): void {
    const imgs = this.list.querySelectorAll<HTMLImageElement>('img[data-src]');
    if (!('IntersectionObserver' in window)) { for (const img of imgs) hydrate(img); return; }
    const io = (this.io ??= new IntersectionObserver((entries) => {
      for (const e of entries) if (e.isIntersecting) { hydrate(e.target as HTMLImageElement); io.unobserve(e.target); }
    }, { root: this.list, rootMargin: '120px 0px' }));
    io.disconnect();
    if (this.side.inert) return;
    for (const img of imgs) io.observe(img);
    if ('requestIdleCallback' in window) this.warm();
  }

  private warm(): void {
    if (this.idle) cancelIdleCallback(this.idle);
    this.idle = requestIdleCallback(() => {
      this.idle = 0;
      if (this.side.inert) return;
      for (const img of this.list.querySelectorAll<HTMLImageElement>('img[data-src]')) hydrate(img);
    }, { timeout: 1500 });
  }
}

function hydrate(img: HTMLImageElement): void {
  const { src, srcset } = img.dataset;
  if (!src) return;
  const show = () => img.classList.add('is-in');
  img.addEventListener('load', show, { once: true });
  // srcset first: a bare srcset on a src-less img fetches at once.
  if (srcset) { img.srcset = srcset; delete img.dataset.srcset; }
  img.src = src;
  delete img.dataset.src;
  if (img.complete && img.naturalWidth) show();
}
