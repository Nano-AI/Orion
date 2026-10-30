/*  Orion site: the hero's one moment. A seven-bladed iris lies closed over
 *  the app's window when the page opens and opens on it, once, in 850 ms,
 *  then leaves the DOM; the window's points fade in after it. Each blade is a
 *  wedge from the centre that swings about its outer corner, the way an
 *  iris blade swings about its pin, so the opening turns as it grows. Without
 *  anime.js, or with reduced motion, there is no iris: the window is just there.
 */
(() => {
  'use strict';
  const M = window.OrionMotion;
  const fig = document.querySelector('[data-shot]');
  if (!M || !M.ok || !fig) return;
  const { A, EASE, once } = M;

  const N = 7;                              // blades
  const R = 150;                            // the iris's radius, past the window's far corners (viewBox units)
  const WEDGE = (2 * Math.PI) / N + 0.3;    // each blade overlaps the next by 0.3 rad, so it closes with no gap
  const SWING = 80;                         // degrees each blade turns about its pin to open
  const CY = -25;                           // the iris centres 30% down the window, so it opens above the fold
  const NS = 'http://www.w3.org/2000/svg';

  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('class', 'shot__iris');
  svg.setAttribute('viewBox', '-100 -62.5 200 125');   // the window's 16:10
  svg.setAttribute('preserveAspectRatio', 'xMidYMid slice');
  svg.setAttribute('aria-hidden', 'true');
  const d = `M0,0 L${R},0 L${(R * Math.cos(WEDGE)).toFixed(2)},${(R * Math.sin(WEDGE)).toFixed(2)} Z`;
  const iris = document.createElementNS(NS, 'g');
  iris.setAttribute('transform', `translate(0 ${CY})`);
  svg.append(iris);
  const blades = Array.from({ length: N }, () => {
    const p = document.createElementNS(NS, 'path');
    p.setAttribute('d', d);
    p.setAttribute('vector-effect', 'non-scaling-stroke');
    return iris.appendChild(p);
  });
  const pose = t => blades.forEach((p, i) => p.setAttribute('transform', `rotate(${(360 / N) * i}) rotate(${SWING * t}, ${R}, 0)`));
  pose(0);
  fig.classList.add('is-iris');
  fig.append(svg);

  const s = { t: 0 };
  once(fig, () => A.animate(s, {
    t: 1, duration: 850, delay: 150, ease: EASE,
    onUpdate: () => { pose(s.t); svg.style.opacity = s.t < 0.55 ? 1 : 1 - (s.t - 0.55) / 0.45; },
    onComplete: () => {
      svg.remove();
      fig.classList.remove('is-iris');
      A.animate(fig.querySelectorAll('.spot button'), { opacity: [0, 1], scale: [0.6, 1], duration: 240, delay: A.stagger(50), ease: EASE });
    },
  }), 0.2);
})();
