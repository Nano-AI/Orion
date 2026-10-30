/*  Orion site: the first row's loupe (develop.css). One raw frame as shot, and a
 *  circle that shows the same frame developed wherever it sits: the loupe
 *  moves one way and the photograph inside it the other, so the developed
 *  frame lines up exactly with the one under it.
 *
 *  Until someone points at it the loupe drifts slowly across the frame, so
 *  it is plain the photograph does something; from then on it follows the
 *  pointer, easing after it. It only moves once both photographs are
 *  decoded, only by transform, once per frame, and never reads layout while
 *  it moves: sizes come from a ResizeObserver, the pointer from offsetX.
 *  With reduced motion it does not drift and follows without easing.
 */
(() => {
  'use strict';
  const fig = document.querySelector('[data-develop]');
  const frame = fig && fig.querySelector('.develop__frame');
  if (!frame || !('ResizeObserver' in window)) return;
  const loupe = frame.querySelector('.loupe');
  const inner = loupe.querySelector('img');
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
  let W = 0, H = 0, R = 0, x = 0, y = 0, tx = 0, ty = 0;
  let raf = 0, live = false, user = false, inView = false;
  const t0 = performance.now();

  const place = () => {
    loupe.style.transform = `translate3d(${x - R}px, ${y - R}px, 0)`;
    inner.style.transform = `translate3d(${R - x}px, ${R - y}px, 0)`;
  };
  // a slow figure of eight through the lower middle, where the shadows are
  const drift = now => {
    const t = (now - t0) / 1000;
    tx = W * (0.5 + 0.34 * Math.sin(t * 0.42));
    ty = H * (0.6 + 0.16 * Math.sin(t * 0.84));
  };
  const tick = now => {
    raf = 0;
    const drifting = !user && !still && inView;
    if (drifting) drift(now);
    const k = still ? 1 : drifting ? 0.08 : 0.2;
    x += (tx - x) * k;
    y += (ty - y) * k;
    place();
    if (drifting || Math.abs(tx - x) + Math.abs(ty - y) > 0.3) raf = requestAnimationFrame(tick);
  };
  const go = () => { if (live && !raf) raf = requestAnimationFrame(tick); };

  new ResizeObserver(([e]) => {
    W = e.contentRect.width; H = e.contentRect.height; R = loupe.offsetWidth / 2;
    frame.style.setProperty('--fw', `${W}px`);
    frame.style.setProperty('--fh', `${H}px`);
    if (!x) { x = tx = W / 2; y = ty = H / 2; }
    place();
  }).observe(frame);

  // the frame is the only thing under the pointer (develop.css), so offsetX is relative to it
  frame.addEventListener('pointermove', e => { user = true; tx = e.offsetX; ty = e.offsetY; go(); });
  frame.addEventListener('pointerleave', () => { user = false; go(); });
  new IntersectionObserver(([e]) => { inView = e.isIntersecting; go(); }).observe(frame);

  const shot = frame.querySelector('.develop__shot');
  Promise.all([shot.decode(), inner.decode()]).then(() => {
    live = true;
    fig.classList.add('is-live');
    go();
  }, () => {});   // a photograph that will not decode leaves the frame as shot, and no loupe
})();
