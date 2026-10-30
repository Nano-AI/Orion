/*  Orion site: the motion layer, and all of it. A block marked data-reveal
 *  fades up 12px the first time it enters the screen, once, in 280 ms (the
 *  tokens in base.css). Nothing moves while you read, nothing is tied to the
 *  scroll, and nothing re-plays. With reduced motion asked for the fade
 *  stays and the 12px does not (base.css). Without this file every block is
 *  simply there.
 */
(() => {
  'use strict';
  if (!('IntersectionObserver' in window) || location.search.includes('nomotion')) return;

  const blocks = document.querySelectorAll('[data-reveal]');
  // what is already on screen when the page opens is not hidden first
  const seen = el => el.getBoundingClientRect().top < innerHeight;
  const io = new IntersectionObserver(entries => {
    for (const e of entries) {
      if (!e.isIntersecting) continue;
      e.target.classList.add('is-in');
      io.unobserve(e.target);
    }
  }, { rootMargin: '0px 0px -8% 0px' });
  blocks.forEach(el => (seen(el) ? el.classList.add('is-in') : io.observe(el)));
  document.documentElement.classList.add('motion');
})();
