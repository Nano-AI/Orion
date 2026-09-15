/*  Orion site: the behaviour that has to work without the motion layer.
 *  The nav, the section in view, the compares and the exposure sweep. motion.js is optional on top of this:
 *  if GSAP never loads, this file alone leaves a complete, usable page.
 */
(() => {
  'use strict';

  // A hand on a slider wins: motion.js stops steering a figure once touched.
  const touchable = (fig, input) => {
    const touch = () => { fig.dataset.touched = '1'; };
    input.addEventListener('pointerdown', touch);
    input.addEventListener('keydown', touch);
  };

  // ---------- nav: phone menu ----------
  const nav = document.querySelector('.nav');
  const toggle = nav && nav.querySelector('.nav__toggle');
  if (toggle) {
    const setOpen = open => {
      nav.classList.toggle('is-open', open);
      toggle.setAttribute('aria-expanded', String(open));
    };
    toggle.addEventListener('click', () => setOpen(!nav.classList.contains('is-open')));
    nav.addEventListener('keydown', e => {
      if (e.key === 'Escape' && nav.classList.contains('is-open')) { setOpen(false); toggle.focus(); }
    });
    nav.querySelectorAll('.nav__links a').forEach(a => a.addEventListener('click', () => setOpen(false)));
    document.addEventListener('pointerdown', e => { if (!nav.contains(e.target)) setOpen(false); });
    nav.addEventListener('focusout', e => { if (!nav.contains(e.relatedTarget)) setOpen(false); });
    matchMedia('(max-width: 760px)').addEventListener('change', () => setOpen(false));
    document.documentElement.classList.add('js');
  }

  if ('IntersectionObserver' in window) {
    // ---------- nav: solid once the hero has scrolled under it ----------
    const hero = document.getElementById('top');
    if (nav && hero) {
      new IntersectionObserver(([e]) => nav.classList.toggle('is-solid', !e.isIntersecting),
        { rootMargin: '-72px 0px 0px 0px' }).observe(hero);
    }

    // ---------- nav: aria-current on the section in view ----------
    const links = new Map([...document.querySelectorAll('.nav__links a[href^="#"]')]
      .map(a => [a.hash.slice(1), a]));
    const bar = document.querySelector('.nav__nav');
    let current = null;
    // the lit pill slides to the link for the section in view
    const glow = () => {
      if (!bar) return;
      bar.classList.toggle('has-current', !!current);
      if (!current) return;
      bar.style.setProperty('--ix', `${current.offsetLeft}px`);
      bar.style.setProperty('--iw', `${current.offsetWidth}px`);
    };
    const spy = new IntersectionObserver(entries => {
      for (const e of entries) {
        if (!e.isIntersecting) continue;
        if (current) current.removeAttribute('aria-current');
        current = links.get(e.target.id) || null;
        if (current) current.setAttribute('aria-current', 'true');
        glow();
      }
    }, { rootMargin: '-45% 0px -50% 0px' });
    document.querySelectorAll('main section[id]').forEach(s => spy.observe(s));
    addEventListener('resize', glow);
    if (document.fonts) document.fonts.ready.then(glow);
  }

  // ---------- compare: a native range drives --pos ----------
  document.querySelectorAll('[data-compare]').forEach(fig => {
    const input = fig.querySelector('input[type="range"]');
    if (!input) return;
    const unit = input.dataset.unit || '';
    const set = () => {
      const v = Number(input.value);
      fig.style.setProperty('--pos', `${v}%`);
      fig.classList.toggle('is-end', v <= 0.5 || v >= 99.5);
      fig.classList.toggle('is-low', v < 12);
      fig.classList.toggle('is-high', v > 88);
      input.setAttribute('aria-valuetext', `${Math.round(v)}% ${unit}`.trim());
    };
    input.addEventListener('input', set);
    input.addEventListener('keydown', e => {
      if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(e.key)) return;
      e.preventDefault();
      const direction = ['ArrowRight', 'ArrowUp'].includes(e.key) ? 1 : -1;
      input.value = Number(input.value) + direction * (e.shiftKey ? 10 : 1);
      set();
    });
    touchable(fig, input);
    set();
  });

  // ---------- exposure sweep: one slider crossfades seven real renders ----------
  document.querySelectorAll('[data-sweep]').forEach(fig => {
    const input = fig.querySelector('input[type="range"]');
    const frames = [...fig.querySelectorAll('.sweep__frames img')];
    const out = fig.querySelector('[data-sweep-val]');
    const evs = (fig.dataset.evs || '').split(',').map(Number);
    if (!input || frames.length < 2 || evs.length !== frames.length) return;
    const last = frames.length - 1;
    const fmt = ev => (Math.abs(ev) < 0.005 ? '0.00 EV' : `${ev < 0 ? '−' : '+'}${Math.abs(ev).toFixed(2)} EV`);
    // where 0 EV sits on the rail, as a fraction: the groove fills from there to the handle
    let zero = 0;
    for (let i = 0; i < last; i++) if (evs[i] <= 0 && evs[i + 1] >= 0) { zero = (i - evs[i] / (evs[i + 1] - evs[i])) / last; break; }
    fig.style.setProperty('--zero', zero);
    const set = () => {
      const v = Math.min(last, Math.max(0, Number(input.value)));
      const i = Math.min(last - 1, Math.floor(v));
      const t = v - i;
      frames.forEach((f, k) => { f.style.opacity = k <= i ? 1 : k === i + 1 ? t : 0; });
      const ev = evs[i] + (evs[i + 1] - evs[i]) * t;
      if (out) {
        out.textContent = fmt(ev);
        out.classList.toggle('is-changed', Math.abs(ev) >= 0.005);
      }
      input.setAttribute('aria-valuetext', fmt(ev));
      fig.style.setProperty('--lo', Math.min(v / last, zero));
      fig.style.setProperty('--hi', Math.max(v / last, zero));
    };
    input.addEventListener('input', set);
    touchable(fig, input);
    set();
  });
})();
