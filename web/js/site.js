/*  Orion site: the phone menu, the section in view, the download buttons'
 *  label, the compare, and the exposure sweep in the hero. Everything on the
 *  page is finished without this file; it only makes the controls work.
 */
(() => {
  'use strict';
  document.documentElement.classList.add('js');
  const still = matchMedia('(prefers-reduced-motion: reduce)').matches;   // nothing moves on its own

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
    // until someone touches a slider, Exposure breathes on its own between -1 and +1 EV
    if (!still) {
      let idle = true, t0 = 0, raf = 0;
      const stop = () => { idle = false; cancelAnimationFrame(raf); };
      [ctl, grab].forEach(i => i && ['pointerdown', 'keydown', 'focus'].forEach(ev => i.addEventListener(ev, stop)));
      const breathe = now => { if (!idle) return; t0 = t0 || now; show(Math.sin((now - t0) / 2600)); raf = requestAnimationFrame(breathe); };
      new IntersectionObserver(es => { cancelAnimationFrame(raf); if (idle && es[0].isIntersecting) raf = requestAnimationFrame(breathe); }).observe(fig);
    }
  });

  // ---------- the compare wipes once on its own when it comes into view ----------
  if (!still) document.querySelectorAll('[data-compare]').forEach(fig => {
    const input = fig.querySelector('input[type="range"]');
    let touched = false;
    input.addEventListener('pointerdown', () => { touched = true; }, { once: true });
    const io = new IntersectionObserver(es => {
      if (!es[0].isIntersecting || touched) return;
      io.disconnect();
      const t0 = performance.now();
      const step = now => {
        if (touched) return;
        const p = Math.min(1, (now - t0) / 3200);
        input.value = 50 + 22 * Math.sin(p * Math.PI * 2) * (1 - p * 0.3);
        input.dispatchEvent(new Event('input'));
        if (p < 1) requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
    }, { threshold: 0.7 });
    io.observe(fig);
  });

  // ---------- the numbers count up once, when they come into view ----------
  if (!still) {
    const io = new IntersectionObserver(es => es.forEach(e => {
      if (!e.isIntersecting) return;
      io.unobserve(e.target);
      const b = e.target, m = b.textContent.match(/[\d,]+/);
      if (!m) return;
      const n = Number(m[0].replace(/,/g, '')), [pre, post] = b.textContent.split(m[0]), t0 = performance.now();
      const step = now => {
        const p = Math.min(1, (now - t0) / 1200);
        b.textContent = pre + Math.round(n * (1 - Math.pow(1 - p, 3))).toLocaleString('en-US') + post;
        if (p < 1) requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
    }), { threshold: 0.6 });
    document.querySelectorAll('.facts b').forEach(b => io.observe(b));
  }

  // ---------- things below the fold rise in as they arrive ----------
  const rv = new IntersectionObserver(es => es.forEach(e => { if (e.isIntersecting) { e.target.classList.add('is-in'); rv.unobserve(e.target); } }), { rootMargin: '0px 0px -10% 0px' });
  document.querySelectorAll('.section__head, .cmp, .files > div, .build__group, .build__cite, .facts li').forEach((el, i) => { el.classList.add('reveal'); el.style.transitionDelay = `${(i % 4) * 70}ms`; rv.observe(el); });

  // ---------- copy buttons ----------
  document.querySelectorAll('[data-copy]').forEach(b => {
    if (!navigator.clipboard) { b.hidden = true; return; }
    b.addEventListener('click', () => navigator.clipboard.writeText(b.dataset.copy).then(() => {
      b.textContent = 'Copied'; b.classList.add('is-done');
      setTimeout(() => { b.textContent = 'Copy'; b.classList.remove('is-done'); }, 1600);
    }));
  });

  // ---------- feature cards: the glow sits under the pointer ----------
  document.querySelectorAll('.build__group').forEach(c => c.addEventListener('pointermove', e => {
    const r = c.getBoundingClientRect();
    c.style.setProperty('--mx', `${e.clientX - r.left}px`);
    c.style.setProperty('--my', `${e.clientY - r.top}px`);
  }));

  // ---------- the lights: bodies with momentum in a soft gravity well ----------
  // Each light orbits the centre of its section, pulls weakly on the others,
  // and falls toward the pointer while it is over the section. A speed governor
  // keeps them drifting forever without running away. Positions are in a
  // normalised square (-1..1) stretched to the section, so it scales freely.
  if (!still) document.querySelectorAll('.aurora').forEach(au => {
    const lights = [...au.querySelectorAll('span')];
    const core = au.classList.contains('aurora--core');   // with a sphere, the orbit plane tilts: depth is y, drawn flattened
    if (!lights.length) return;
    au.classList.add('is-sim');
    const host = au.parentElement;
    const TILT = 0.34, GM = 0.143, SOFT = 0.6, G = 0.006, GSOFT = 0.2, PM = 0.2, PSOFT = 0.3, CRUISE = 0.3, MAXV = 0.9;   // a flat rotation curve: about 0.3 a second at any radius from 0.8 out
    const bodies = lights.map((el, i) => {
      const a = (i / lights.length) * Math.PI * 2 + Math.random() * 0.6, r = 0.85 + Math.random() * 0.3;
      const v = Math.sqrt(GM * r * r / Math.pow(r * r + SOFT * SOFT, 1.5)) * (0.9 + Math.random() * 0.2);   // orbital speed, a little off so the orbits are ellipses
      return { el, x: Math.cos(a) * r, y: Math.sin(a) * r, vx: -Math.sin(a) * v, vy: Math.cos(a) * v, m: 0.6 + Math.random() * 0.8 };
    });
    let W = 0, H = 0, pointer = null, raf = 0, last = 0;
    const size = () => { W = au.clientWidth; H = au.clientHeight; bodies.forEach(b => { b.w = b.el.offsetWidth; }); };
    size(); addEventListener('resize', size);
    host.addEventListener('pointermove', e => {
      const r = au.getBoundingClientRect();
      const py = ((e.clientY - r.top) / r.height) * 2 - 1;
      pointer = { x: ((e.clientX - r.left) / r.width) * 2 - 1, y: core ? py / TILT : py };
    });
    host.addEventListener('pointerleave', () => { pointer = null; });
    const pull = (b, x, y, gm, soft) => {   // softened inverse-square, so close passes swing instead of snapping
      const dx = x - b.x, dy = y - b.y, d2 = dx * dx + dy * dy + soft * soft, f = gm / (d2 * Math.sqrt(d2));
      b.ax += dx * f; b.ay += dy * f;
    };
    const step = now => {
      const dt = Math.min(0.033, (now - (last || now)) / 1000); last = now;
      for (const b of bodies) {
        b.ax = 0; b.ay = 0;
        pull(b, 0, 0, GM, SOFT);
        for (const o of bodies) if (o !== b) pull(b, o.x, o.y, G * o.m, GSOFT);
        if (pointer) pull(b, pointer.x, pointer.y, PM, PSOFT);
      }
      for (const b of bodies) {
        b.vx += b.ax * dt; b.vy += b.ay * dt;   // semi-implicit Euler: stable orbits
        const v = Math.hypot(b.vx, b.vy) || 1e-6;
        const k = 1 + (CRUISE - v) / CRUISE * 0.25 * dt * (pointer ? 0.3 : 1);   // drift back to cruising speed
        const s = Math.min(k, MAXV / v);
        b.vx *= s; b.vy *= s;
        b.x += b.vx * dt; b.y += b.vy * dt;
        if (core) {   // y is depth: in front of the sphere below the middle, behind it above
          const d = Math.max(-1, Math.min(1, b.y)), k = 1 + 0.3 * d;
          b.el.style.transform = `translate3d(${(b.x + 1) / 2 * W - b.w / 2}px, ${H / 2 + b.y * TILT * H / 2 - b.w / 2}px, 0) scale(${k})`;
          b.el.style.opacity = 0.55 + 0.45 * (d + 1) / 2;
          b.el.style.zIndex = b.y > 0 ? 2 : 0;
        } else b.el.style.transform = `translate3d(${(b.x + 1) / 2 * W - b.w / 2}px, ${(b.y + 1) / 2 * H - b.w / 2}px, 0)`;
      }
      raf = requestAnimationFrame(step);
    };
    new IntersectionObserver(es => {
      cancelAnimationFrame(raf); last = 0;
      if (es[0].isIntersecting) raf = requestAnimationFrame(step);
    }).observe(au);
  });

  // ---------- the plate tilts a little toward the pointer ----------
  const stage = document.querySelector('.stage');
  if (stage && matchMedia('(hover: hover) and (pointer: fine)').matches && !still) {
    const plate = stage.querySelector('.stage__in');
    stage.addEventListener('pointermove', e => {
      const r = stage.getBoundingClientRect();
      plate.style.setProperty('--ry', `${((e.clientX - r.left) / r.width - 0.5) * 6}deg`);
      plate.style.setProperty('--rx', `${(0.5 - (e.clientY - r.top) / r.height) * 4}deg`);
    });
    stage.addEventListener('pointerleave', () => { plate.style.removeProperty('--rx'); plate.style.removeProperty('--ry'); });
  }

  // ---------- the sky: a few dozen stars behind the hero, two layers that twinkle out of phase ----------
  const hero = document.querySelector('.hero');
  if (hero) {
    const sky = document.createElement('div');
    sky.className = 'sky'; sky.setAttribute('aria-hidden', 'true');
    for (let layer = 0; layer < 2; layer++) {
      const dots = [];
      for (let i = 0; i < 70; i++) dots.push(`${Math.round(Math.random() * 1600 - 80)}px ${Math.round(Math.random() * 720)}px 0 ${Math.random() < 0.3 ? 1 : 0}px`);
      const d = document.createElement('i');
      d.style.boxShadow = dots.join(',');
      sky.append(d);
    }
    if (!still) for (let i = 0; i < 3; i++) {   // three comets; each pass starts somewhere new
      const c = document.createElement('b');
      c.addEventListener('animationiteration', () => { c.style.top = `${5 + Math.random() * 45}%`; c.style.left = `${Math.random() * 70}%`; });
      sky.append(c);
    }
    hero.prepend(sky);
  }
})();
