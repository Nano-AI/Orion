/*  Orion site: the points on the app's window. Pointing at one, focusing it
 *  or tapping it shows what that part of the window does, on a wire anime.js
 *  draws out from the point; on a phone the label is read out under the
 *  window instead. Each point is a real button, named by its label, so the
 *  keyboard and screen readers get the same thing.
 */
(() => {
  'use strict';
  const spots = [...document.querySelectorAll('.spot')];
  if (!spots.length) return;
  const M = window.OrionMotion;
  const readout = document.querySelector('[data-readout]');
  const hover = matchMedia('(hover: hover) and (pointer: fine)').matches;
  const NS = 'http://www.w3.org/2000/svg';
  // from the edge of the 32px point to the label, 64px out (--s-8 in sections.css), on the label's side
  const WIRE = { left: [-16, 0, -64, 0], right: [16, 0, 64, 0], down: [0, 16, 0, 64] };
  let current = null;

  const parts = spots.map(li => {
    const svg = document.createElementNS(NS, 'svg');
    svg.setAttribute('class', 'spot__wire');
    svg.setAttribute('width', '1');
    svg.setAttribute('height', '1');
    svg.setAttribute('aria-hidden', 'true');
    const line = document.createElementNS(NS, 'line');
    const [x1, y1, x2, y2] = WIRE[li.dataset.side] || WIRE.left;
    Object.entries({ x1, y1, x2, y2 }).forEach(([k, v]) => line.setAttribute(k, v));
    svg.append(line);
    li.prepend(svg);
    return { li, line, button: li.querySelector('button'), label: li.querySelector('.spot__label') };
  });

  const show = (part, on) => {
    if (on && current && current !== part) show(current, false);
    part.li.classList.toggle('is-on', on);
    part.line.style.opacity = on ? 1 : 0;
    current = on ? part : current === part ? null : current;
    if (readout) {
      readout.textContent = current ? current.label.textContent : 'Tap a point on the window.';
      readout.classList.toggle('is-on', !!current);
    }
    if (on && M && M.ok) M.A.animate(M.A.createDrawable(part.line), { draw: ['0 0', '0 1'], duration: 220, ease: M.EASE });
  };

  parts.forEach(part => {
    part.line.style.opacity = 0;
    if (hover) {
      part.li.addEventListener('pointerenter', () => show(part, true));
      part.li.addEventListener('pointerleave', () => { if (document.activeElement !== part.button) show(part, false); });
    }
    part.button.addEventListener('focus', () => show(part, true));
    part.button.addEventListener('blur', () => show(part, false));
    // a tap opens; tapping elsewhere (the blur) or Escape closes. Toggling here would undo the focus that a click also fires.
    part.button.addEventListener('click', () => { if (current !== part) show(part, true); });
    part.button.addEventListener('keydown', e => { if (e.key === 'Escape') show(part, false); });
  });
})();
