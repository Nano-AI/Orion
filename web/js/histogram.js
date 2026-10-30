/*  Orion site: the exposure demo's histogram. Each of the seven renders on
 *  the page is measured once, in a small canvas, into 48 bins of luminance
 *  (Rec. 709 weights on the sRGB values, the way a camera's histogram reads a
 *  JPEG); dragging the slider blends the two renders it sits between, and
 *  anime.js moves each bar to its new height. A bar is a scaleY, so nothing
 *  is laid out again. When more than 0.5% of the frame is at 0 or 255 that
 *  end's bar wears the status colour, as the app's clipping warning does. With
 *  reduced motion the bars jump instead of glide; with no canvas they hide.
 */
(() => {
  'use strict';
  const fig = document.querySelector('[data-sweep]');
  const host = fig && fig.querySelector('[data-hist]');
  if (!host) return;
  const M = window.OrionMotion;
  const input = fig.querySelector('input[type="range"]');
  const frames = [...fig.querySelectorAll('.sweep__frames img')];
  const BINS = 48, W = 160, CLIP = 0.005;   // a frame with more than 0.5% of its pixels at 0 or 255 is clipping
  const bars = Array.from({ length: BINS }, () => {
    const b = host.appendChild(document.createElement('i'));
    b.style.transform = 'scaleY(0.02)';   // inline, so anime.js starts each bar from here
    return b;
  });

  const measure = img => {
    const h = Math.round((W * img.naturalHeight) / img.naturalWidth);
    const c = document.createElement('canvas');
    c.width = W; c.height = h;
    const g = c.getContext('2d', { willReadFrequently: true });
    g.drawImage(img, 0, 0, W, h);
    const px = g.getImageData(0, 0, W, h).data, n = new Float32Array(BINS + 2);   // the bins, then the clipped dark and light
    for (let i = 0; i < px.length; i += 4) {
      const y = 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
      n[Math.min(BINS - 1, Math.floor((y / 256) * BINS))]++;
      if (y < 1) n[BINS]++; else if (y > 254) n[BINS + 1]++;
    }
    return n.map(v => v / (W * h));   // a share of the frame
  };

  let hists = null;
  const at = v => {
    const i = Math.min(hists.length - 2, Math.floor(v)), t = v - i;
    return hists[i].map((a, k) => a + (hists[i + 1][k] - a) * t);
  };
  const draw = (glide, first) => {
    const h = at(Math.min(frames.length - 1, Math.max(0, Number(input.value))));
    // scaled to the tallest bar between the two ends, as a camera does, so a clipped end does not flatten the rest; sqrt, so the tones read
    const peak = Math.max(...h.slice(1, BINS - 1)) || 1;
    const scale = k => Math.min(1, Math.max(0.02, Math.sqrt(h[k] / peak)));
    bars[0].classList.toggle('is-clip', h[BINS] > CLIP);
    bars[BINS - 1].classList.toggle('is-clip', h[BINS + 1] > CLIP);
    if (glide && M && M.ok) {
      M.A.animate(bars, { scaleY: (el, k) => scale(k), duration: first ? 420 : 180, delay: first ? M.A.stagger(8) : 0, ease: M.EASE });
    } else {
      bars.forEach((b, k) => { b.style.transform = `scaleY(${scale(k)})`; });
    }
  };

  const start = () => Promise.all(frames.map(f => (f.complete && f.naturalWidth ? f.decode() : new Promise(r => f.addEventListener('load', r, { once: true })))))
    .then(() => {
      hists = frames.map(measure);
      input.addEventListener('input', () => draw(true, false));
      draw(true, true);
    })
    .catch(() => host.remove());   // a canvas the page may not read (file://), or a frame that failed

  // measure only when the demo is near, so the seven frames load as they would anyway
  if ('IntersectionObserver' in window) {
    const near = new IntersectionObserver(([e]) => { if (e.isIntersecting) { near.disconnect(); start(); } }, { rootMargin: '200px' });
    near.observe(fig);
  } else start();
})();
