/*  Orion site: the phone menu, the section in view, the download buttons'
 *  label, the compare, and the exposure sweep in the hero. Everything on the
 *  page is finished without this file; it only makes the controls work.
 */
(() => {
  'use strict';
  document.documentElement.classList.add('js');

  // ---------- nav: phone menu ----------
  const nav = document.querySelector('.nav');
  const toggle = nav && nav.querySelector('.nav__toggle');
  if (toggle) {
    const setOpen = open => { nav.classList.toggle('is-open', open); toggle.setAttribute('aria-expanded', String(open)); };
    toggle.addEventListener('click', () => setOpen(!nav.classList.contains('is-open')));
    nav.addEventListener('keydown', e => { if (e.key === 'Escape' && nav.classList.contains('is-open')) { setOpen(false); toggle.focus(); } });
    nav.querySelectorAll('.nav__links a').forEach(a => a.addEventListener('click', () => setOpen(false)));
    document.addEventListener('pointerdown', e => { if (!nav.contains(e.target)) setOpen(false); });
    nav.addEventListener('focusout', e => { if (!nav.contains(e.relatedTarget)) setOpen(false); });
    matchMedia('(max-width: 760px)').addEventListener('change', () => setOpen(false));
  }

  // ---------- the download buttons name this computer's system ----------
  // Only a Mac has a build today; anyone else gets a plain "Download" and the
  // download page says what exists for them. iPadOS reports a Mac, with touch.
  const ua = navigator.userAgentData;
  const platform = (ua && ua.platform) || navigator.platform || '';
  const mac = /Mac/i.test(platform) && navigator.maxTouchPoints < 2 && !/iPhone|iPad|Android/i.test(navigator.userAgent);
  if (!mac) document.querySelectorAll('[data-os-label]').forEach(a => { a.textContent = 'Download'; });

  // ---------- nav: aria-current on the section in view ----------
  if ('IntersectionObserver' in window) {
    const links = new Map([...document.querySelectorAll('.nav__links a[href^="#"]')].map(a => [a.hash.slice(1), a]));
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

  // arrow keys step a slider by 1, shift by 10, whatever its step attribute
  const keys = (input, set) => input.addEventListener('keydown', e => {
    if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'].includes(e.key)) return;
    e.preventDefault();
    const dir = ['ArrowRight', 'ArrowUp'].includes(e.key) ? 1 : -1;
    const unit = Number(input.dataset.key || 1);
    input.value = Number(input.value) + dir * unit * (e.shiftKey ? 10 : 1);
    set();
  });

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
    keys(input, set);
    set();
  });

  // ---------- exposure sweep: two sliders, one value, seven real renders ----------
  // The figure holds the frames and an invisible range over the app's own
  // Exposure slider; the control under it is the same value, drawn. Each frame
  // is an opacity, so a drag never leaves the compositor.
  document.querySelectorAll('[data-sweep]').forEach(fig => {
    const frames = [...fig.querySelectorAll('img')];
    const grab = fig.querySelector('input[type="range"]');   // in EV, the app's own range, clamped to the frames
    const ctl = fig.parentElement.querySelector('.range');   // 0 to frames - 1
    const out = fig.parentElement.querySelector('[data-sweep-val]');
    const evs = (fig.dataset.evs || '').split(',').map(Number);
    if (!ctl || frames.length < 2 || evs.length !== frames.length) return;
    const last = frames.length - 1, lo = evs[0], hi = evs[last];
    const zero = evs.indexOf(0) / last;
    const fmt = ev => (Math.abs(ev) < 0.005 ? '0.00 EV' : `${ev < 0 ? '\u2212' : '+'}${Math.abs(ev).toFixed(2)} EV`);
    const show = ev => {
      ev = Math.min(hi, Math.max(lo, ev));
      const v = (ev - lo) / (hi - lo) * last;   // the frames are evenly spaced in EV
      const i = Math.min(last - 1, Math.floor(v)), t = v - i;
      frames.forEach((f, k) => { f.style.opacity = k <= i ? 1 : k === i + 1 ? t : 0; });
      ctl.value = v; if (grab) grab.value = ev;
      ctl.style.setProperty('--lo', Math.min(v / last, zero));
      ctl.style.setProperty('--hi', Math.max(v / last, zero));
      if (out) { out.textContent = fmt(ev); out.classList.toggle('is-changed', Math.abs(ev) >= 0.005); }
      [ctl, grab].forEach(i => i && i.setAttribute('aria-valuetext', fmt(ev)));
    };
    ctl.dataset.key = '0.1';
    ctl.addEventListener('input', () => show(lo + Number(ctl.value) / last * (hi - lo)));
    keys(ctl, () => show(lo + Number(ctl.value) / last * (hi - lo)));
    if (grab) {
      grab.dataset.key = '0.05';
      grab.addEventListener('input', () => show(Number(grab.value)));
      keys(grab, () => show(Number(grab.value)));
    }
    // a double click on either slider is the app's "back to zero"
    [ctl, grab].forEach(i => i && i.addEventListener('dblclick', () => show(0)));
    show(0);
  });
})();
