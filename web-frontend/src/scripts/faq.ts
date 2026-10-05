export function initFaq(): void {
  const items = [...document.querySelectorAll<HTMLElement>('.faq-item')];
  const setOpen = (item: HTMLElement, open: boolean) => {
    item.classList.toggle('is-open', open);
    item.querySelector('.faq-q')!.setAttribute('aria-expanded', String(open));
  };
  for (const item of items) {
    item.querySelector('.faq-q')!.addEventListener('click', () => {
      const wasOpen = item.classList.contains('is-open');
      for (const other of items) setOpen(other, false);
      if (!wasOpen) setOpen(item, true);
    });
  }
}
