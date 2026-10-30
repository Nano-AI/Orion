/*  Orion site: the motion layer's shared part, and its small moments.
 *
 *  Every moment plays once, moves only transform, opacity or SVG, pauses
 *  while it is off screen, and is about photography: an iris opening on the
 *  app (aperture.js), wires to the window's parts (spots.js), a histogram
 *  that follows the exposure slider (histogram.js), and here a camera's LCD
 *  readout, a frame developing, a shutter on the Download button, and a
 *  12px fade-up the first time a block is seen. The HTML already holds every
 *  final state, so no anime.js, reduced motion or ?nomotion leaves the page
 *  complete and still. Durations and the curve are base.css's tokens.
 */
window.OrionMotion = (() => {
  'use strict';
  const A = window.anime;
  const io = 'IntersectionObserver' in window;
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches || location.search.includes('nomotion');
  const ok = !!A && io && !reduced;
  const EASE = A ? A.cubicBezier(0.23, 1, 0.32, 1) : null;   // --ease-out

  // start() runs the first time el is on screen; what it returns pauses while el is off it
  const once = (el, start, threshold = 0.35) => {
    let anim = null;
    const watch = new IntersectionObserver(([e]) => {
      if (e.isIntersecting && !anim) {
        anim = start();
        if (anim && anim.then) anim.then(() => watch.disconnect()); else watch.disconnect();
      } else if (anim && !anim.completed) {
        e.isIntersecting ? anim.play() : anim.pause();
      }
    }, { threshold });
    watch.observe(el);
  };

  // ---------- the fade-up: once, 12px, 280 ms (with reduced motion, the fade alone) ----------
  if (io && !location.search.includes('nomotion')) {
    const seen = el => el.getBoundingClientRect().top < innerHeight;
    const watch = new IntersectionObserver(entries => {
      for (const e of entries) if (e.isIntersecting) { e.target.classList.add('is-in'); watch.unobserve(e.target); }
    }, { rootMargin: '0px 0px -8% 0px' });
    document.querySelectorAll('[data-reveal]').forEach(el => (seen(el) ? el.classList.add('is-in') : watch.observe(el)));
    document.documentElement.classList.add('motion');
  }
  if (!ok) return { A, ok, EASE, once };

  // ---------- 18.9 ms rolls up the way a camera's LCD counts ----------
  const lcd = document.querySelector('[data-lcd]');
  if (lcd) {
    // everything before the unit is the number; the HTML already sets its full stop in a narrow cell
    const digits = [...lcd.childNodes].filter(n => n.nodeName !== 'SMALL');
    const cells = [...digits.map(n => n.textContent).join('')].map(ch => {
      const cell = document.createElement('span');
      if (!/\d/.test(ch)) { cell.className = 'lcd__cell lcd__cell--dot'; cell.textContent = ch; return cell; }
      cell.className = 'lcd__cell';
      cell.innerHTML = `<span class="lcd__reel" data-d="${ch}">${'0123456789'.slice(0, +ch + 1).split('').map(d => `<span>${d}</span>`).join('')}</span>`;
      return cell;
    });
    digits.forEach(n => n.remove());
    lcd.prepend(...cells);
    const reels = lcd.querySelectorAll('.lcd__reel');
    once(lcd, () => A.animate(reels, { translateY: el => `${-0.9 * el.dataset.d}em`, duration: 700, delay: A.stagger(70), ease: EASE }));
  }

  // ---------- RAW: the frame as shot develops, and the handle settles in the middle ----------
  // Developed is right of the handle, so it comes in from the right edge to the centre.
  const develop = document.querySelector('[data-develop]');
  const range = develop && develop.querySelector('input[type="range"]');
  if (range) {
    let touched = false;
    ['pointerdown', 'keydown'].forEach(t => range.addEventListener(t, () => { touched = true; }, { once: true }));
    const to = Number(range.value), s = { v: 100 };
    const move = () => { if (touched) return; range.value = s.v; range.dispatchEvent(new Event('input')); };
    move();
    once(develop, () => A.animate(s, { v: to, duration: 900, ease: EASE, onUpdate: move }), 0.5);
  }

  // ---------- Download: a shutter closes and opens across the button you press ----------
  document.querySelectorAll('.btn:not(.btn--ghost)').forEach(button => button.addEventListener('pointerdown', () => {
    let shutter = button.querySelector('.btn__shutter');
    if (!shutter) {
      shutter = document.createElement('span');
      shutter.className = 'btn__shutter';
      shutter.setAttribute('aria-hidden', 'true');
      shutter.innerHTML = '<i></i><i></i>';
      button.append(shutter);
    }
    A.animate(shutter.children, { scaleY: [0, 1, 0], duration: 160, ease: 'inOutQuad' });
  }));

  return { A, ok, EASE, once };
})();
