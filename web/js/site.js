/*  Orion site: the behaviour every page shares.
 *  The phone menu, the section in view, the download buttons' label, the
 *  bar's ground, and the window's parts naming themselves. develop.js is
 *  the loupe.
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

  // ---------- nav: clear over the top of the page, the ground once the page moves under it ----------
  if (nav && 'IntersectionObserver' in window) {
    const top = document.createElement('div');
    top.setAttribute('aria-hidden', 'true');
    top.style.cssText = 'position:absolute;top:0;left:0;width:1px;height:8px;pointer-events:none';
    document.body.prepend(top);
    new IntersectionObserver(([e]) => nav.classList.toggle('is-stuck', !e.isIntersecting)).observe(top);
  }

  // ---------- the window: each outlined part says what it is ----------
  // On a wide screen the label hangs off the part (sections.css); on a phone
  // it is the line under the plate. The tour is the hero's one moment. The first time the window is well in
  // view each part takes a turn, once, so it is plain there is something to
  // point at; the moment anyone points, taps or tabs, the tour stops for good.
  const say = document.querySelector('[data-say]');
  const shot = say && say.closest('.shot');
  if (shot) {
    const hints = [...shot.querySelectorAll('.hint')];
    let on = null, tour = 0;
    const set = hint => {
      if (on) on.classList.remove('is-on');
      on = hint;
      if (hint) hint.classList.add('is-on');
      say.textContent = hint ? hint.getAttribute('aria-label') : '';
    };
    const stop = () => { clearTimeout(tour); tour = -1; };
    hints.forEach(h => {
      h.addEventListener('pointerenter', () => { stop(); set(h); });
      h.addEventListener('pointerleave', () => set(document.activeElement.matches('.hint') ? document.activeElement : null));
      h.addEventListener('focus', () => { stop(); set(h); });
      h.addEventListener('blur', () => set(null));
      h.addEventListener('click', () => { stop(); set(h); });   // a tap ends with a pointerleave, then this
    });
    if ('IntersectionObserver' in window) {
      const seen = new IntersectionObserver(([e]) => {
        if (!e.isIntersecting || tour === -1) return;
        seen.disconnect();
        const shown = hints.filter(h => h.offsetParent);   // the phone hides the title bar's two
        const step = k => { if (tour === -1) return; set(shown[k] || null); if (k < shown.length) tour = setTimeout(step, 2200, k + 1); };
        tour = setTimeout(step, 600, 0);
      }, { threshold: 0.35 });
      seen.observe(shot);
    }
  }
})();
