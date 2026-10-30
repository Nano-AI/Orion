/*  Orion site: the behaviour that has to work without the motion layer.
 *  The phone menu, the section in view, the download buttons' label, the
 *  compares and the exposure sweep. motion.js only fades blocks in on top.
 */
(() => {
  'use strict';

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

  // ---------- the download buttons name this computer's system ----------
  // Only a Mac has a build today; anyone else gets a plain "Download" and the
  // download page says what exists for them. iPadOS reports a Mac, with touch.
  const ua = navigator.userAgentData;
  const platform = (ua && ua.platform) || navigator.platform || '';
  const mac = /Mac/i.test(platform) && navigator.maxTouchPoints < 2 && !/iPhone|iPad|Android/i.test(navigator.userAgent);
  if (!mac) document.querySelectorAll('[data-os-label]').forEach(a => { a.textContent = 'Download'; });

  // ---------- nav: aria-current, and the accent, on the section in view ----------
  if ('IntersectionObserver' in window) {
    const links = new Map([...document.querySelectorAll('.nav__links a[href^="#"]')]
      .map(a => [a.hash.slice(1), a]));
    let current = null;
    const spy = new IntersectionObserver(entries => {
      for (const e of entries) {
        if (!e.isIntersecting) continue;
        if (current) current.removeAttribute('aria-current');
        current = links.get(e.target.id) || null;
        if (current) current.setAttribute('aria-current', 'true');
      }
    }, { rootMargin: '-45% 0px -50% 0px' });
    document.querySelectorAll('main section[id]').forEach(s => spy.observe(s));
  }

  // ---------- compare: a native range drives --pos ----------
  document.querySelectorAll('[data-compare]').forEach(fig => {
    const input = fig.querySelector('input[type="range"]');
    if (!input) return;
    const unit = input.dataset.unit || '';
    const set = () => {
      const v = Number(input.value);
      fig.style.setProperty('--pos', `${v}%`);
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
    set();
  });
})();
