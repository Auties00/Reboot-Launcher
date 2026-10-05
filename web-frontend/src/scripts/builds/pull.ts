import { reduceMotion } from './util';

const MAX_PULL = 160;

export class PullStretch {
  private pull = 0;
  private timer = 0;
  private touchY = -1;

  constructor(
    private readonly scroller: HTMLElement,
    private readonly head: HTMLElement,
    private readonly hero: HTMLElement,
  ) {
    scroller.addEventListener('wheel', (e) => {
      if (!this.canPull() || e.deltaY >= 0) { if (this.pull) this.release(); return; }
      head.classList.add('is-pulling');
      this.set(this.pull - e.deltaY * 0.55);
      clearTimeout(this.timer);
      this.timer = window.setTimeout(() => this.release(), 140);
    }, { passive: true });
    scroller.addEventListener('touchstart', (e) => {
      this.touchY = scroller.scrollTop === 0 ? e.touches[0].clientY : -1;
    }, { passive: true });
    scroller.addEventListener('touchmove', (e) => {
      if (this.touchY < 0 || !this.canPull()) return;
      const dy = e.touches[0].clientY - this.touchY;
      if (dy <= 0) { if (this.pull) this.release(); return; }
      head.classList.add('is-pulling');
      this.set(dy * 0.5);
    }, { passive: true });
    const end = () => { this.touchY = -1; this.release(); };
    scroller.addEventListener('touchend', end, { passive: true });
    scroller.addEventListener('touchcancel', end, { passive: true });
  }

  private canPull(): boolean {
    return !reduceMotion() && !this.hero.classList.contains('no-art') && this.scroller.scrollTop === 0;
  }

  private set(v: number): void {
    this.pull = Math.max(0, Math.min(MAX_PULL, v));
    this.head.style.setProperty('--bl-pull', String(this.pull));
  }

  private release(): void {
    clearTimeout(this.timer);
    this.head.classList.remove('is-pulling');
    this.set(0);
  }
}
