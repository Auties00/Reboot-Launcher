const glide = (): ScrollBehavior => (matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth');

class Backdrop {
  private active = 0;
  private current: string | null = null;

  constructor(private readonly layers: [HTMLImageElement, HTMLImageElement]) {}

  show(src: string | undefined): void {
    if (!src || src === this.current) return;
    this.current = src;
    const show = this.layers[1 - this.active], hide = this.layers[this.active];
    show.src = src;
    const go = () => { show.classList.add('on'); hide.classList.remove('on'); };
    if (show.complete && show.naturalWidth) go();
    else show.onload = go;
    this.active = 1 - this.active;
  }
}

class Reel {
  private readonly films: HTMLElement[];
  private readonly prev: HTMLButtonElement | null;
  private readonly next: HTMLButtonElement | null;
  private focused: HTMLElement | null = null;
  private raf = 0;
  private pendingPaint = false;
  // Snap corrections fire scroll events on load; only a reel someone touched repaints the backdrop.
  private touched = false;
  private drag: { x: number; left: number } | null = null;
  private moved = false;

  constructor(private readonly el: HTMLElement, private readonly backdrop: Backdrop) {
    const group = el.closest<HTMLElement>('.reel-group')!;
    this.films = [...el.querySelectorAll<HTMLElement>('.film')];
    this.prev = group.querySelector('.reel-prev');
    this.next = group.querySelector('.reel-next');

    const touch = () => { this.touched = true; };
    for (const type of ['pointerdown', 'wheel', 'touchstart', 'keydown', 'focusin']) el.addEventListener(type, touch, { passive: true });
    el.addEventListener('scroll', () => this.schedule(this.touched), { passive: true });

    for (const film of this.films) film.addEventListener('mouseenter', () => backdrop.show(film.dataset.img));
    el.addEventListener('mouseleave', () => { if (this.focused) backdrop.show(this.focused.dataset.img); });

    const page = (dir: 1 | -1) => () => {
      this.touched = true;
      el.scrollBy({ left: dir * this.pageWidth(), behavior: glide() });
    };
    this.prev?.addEventListener('click', page(-1));
    this.next?.addEventListener('click', page(1));

    el.addEventListener('pointerdown', (e) => {
      if (e.pointerType !== 'mouse') return;
      this.drag = { x: e.clientX, left: el.scrollLeft };
      this.moved = false;
      el.classList.add('dragging');
    });
    // Capture phase, so the modal's opener sees a drag's click as handled.
    el.addEventListener('click', (e) => {
      if (this.moved) { e.preventDefault(); this.moved = false; }
    }, true);
  }

  get dragging(): boolean { return this.drag !== null; }

  dragTo(clientX: number): void {
    if (!this.drag) return;
    const dx = clientX - this.drag.x;
    if (Math.abs(dx) > 3) this.moved = true;
    this.el.scrollLeft = this.drag.left - dx;
  }

  dragEnd(): void {
    if (!this.drag) return;
    this.drag = null;
    this.el.classList.remove('dragging');
    if (this.moved) this.el.scrollTo({ left: this.el.scrollLeft, behavior: glide() });
  }

  private pageWidth(): number {
    return (this.films[0].getBoundingClientRect().width + 16) * 3;
  }

  schedule(paint: boolean): void {
    this.pendingPaint ||= paint;
    if (!this.raf) this.raf = requestAnimationFrame(() => this.update());
  }

  update(paint = this.pendingPaint): void {
    this.raf = 0;
    this.pendingPaint = false;
    const r = this.el.getBoundingClientRect();
    const mid = r.left + r.width / 2;
    let best: HTMLElement | null = null, bestD = Infinity;
    for (const film of this.films) {
      const b = film.getBoundingClientRect();
      const d = Math.abs(b.left + b.width / 2 - mid);
      if (d < bestD) { bestD = d; best = film; }
    }
    for (const film of this.films) film.classList.toggle('is-focus', film === best);
    this.focused = best;
    if (paint && best) this.backdrop.show(best.dataset.img);
    const max = this.el.scrollWidth - this.el.clientWidth;
    if (this.prev) this.prev.disabled = this.el.scrollLeft <= 4;
    if (this.next) this.next.disabled = this.el.scrollLeft >= max - 4;
  }
}

export function initReels(): void {
  const els = [...document.querySelectorAll<HTMLElement>('.reel')];
  const a = document.querySelector<HTMLImageElement>('.backdrop-a');
  const b = document.querySelector<HTMLImageElement>('.backdrop-b');
  if (!els.length || !a || !b) return;

  const backdrop = new Backdrop([a, b]);
  const reels = els.map((el) => new Reel(el, backdrop));

  addEventListener('pointermove', (e) => { for (const reel of reels) if (reel.dragging) reel.dragTo(e.clientX); });
  addEventListener('pointerup', () => { for (const reel of reels) reel.dragEnd(); });
  addEventListener('resize', () => { for (const reel of reels) reel.schedule(false); });

  reels.forEach((reel, i) => reel.update(i === 0));
}
