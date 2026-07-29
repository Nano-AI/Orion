/* Orion landing — no build step. Three jobs:
   1. theme: an explicit choice beats the system, in both directions
   2. entrance + scroll reveals, via the vendored Motion One (web/vendor/)
   3. the hero demo: the Exposure slider drags itself, and then it's yours */

(function () {
  'use strict';

  var root = document.documentElement;
  var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var M = window.Motion; /* vendored; the page must survive without it */

  /* ------------------------------------------------------------ theme --- */

  function store(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }
  function load(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }

  var btn = document.getElementById('themebtn');
  var label = document.getElementById('themelabel');
  var sysLight = window.matchMedia('(prefers-color-scheme: light)');

  function effective() {
    var t = root.getAttribute('data-theme');
    if (t === 'dark' || t === 'light') return t;
    return sysLight.matches ? 'light' : 'dark';
  }
  function reflect() {
    var t = effective();
    if (label) label.textContent = t === 'dark' ? 'Dark' : 'Light';
    if (btn) btn.setAttribute('aria-label', 'Switch to ' + (t === 'dark' ? 'light' : 'dark') + ' theme');
  }
  reflect();

  if (btn) {
    btn.addEventListener('click', function () {
      var next = effective() === 'dark' ? 'light' : 'dark';
      root.setAttribute('data-theme', next);
      store('orion-theme', next);
      reflect();
    });
  }
  sysLight.addEventListener('change', function () { if (!load('orion-theme')) reflect(); });

  /* ------------------------------------------------- entrance + reveals --- */

  /* Skip the entrance when the page loads in a background tab (frozen
     timelines would hold everything at opacity 0) or with ?static for
     debugging. The page ships its finished state either way. */
  var animOK = !!(M && M.animate) && !reduced && !document.hidden &&
               !/[?&]static/.test(location.search);

  if (animOK) {
    root.classList.add('anim'); /* CSS pre-hides .rev/.hero-in/.mock only now */
    var EASE = [0.22, 0.65, 0.3, 1];

    M.animate('.hero-in',
      { opacity: [0, 1], y: [16, 0] },
      { duration: 0.7, delay: M.stagger(0.09), ease: EASE });

    M.animate('.mock',
      { opacity: [0, 1], y: [28, 0], scale: [0.985, 1] },
      { duration: 0.9, delay: 0.38, ease: EASE });

    if (M.inView) {
      M.inView('[data-rev]', function (el) {
        M.animate(el.querySelectorAll('.rev'),
          { opacity: [0, 1], y: [14, 0] },
          { duration: 0.6, delay: M.stagger(0.07), ease: EASE });
        /* no cleanup returned -> fires once */
      }, { amount: 0.2 });
    } else {
      M.animate('.rev', { opacity: 1 }, { duration: 0 });
    }

    /* Failsafe: if anything kept the reveals from running, un-hide. */
    setTimeout(function () {
      var first = document.querySelector('.hero-in');
      if (first && getComputedStyle(first).opacity === '0') root.classList.remove('anim');
    }, 4000);
  }

  /* ----------------------------------------------------- exposure demo --- */
  /* The markup ships the finished state (+2.60 EV, the render as shot), so
     without JS or with reduced motion the page is simply correct. */

  var track = document.getElementById('exptrack');
  var thumb = document.getElementById('expthumb');
  var fill  = document.getElementById('expfill');
  var val   = document.getElementById('expval');
  var img   = document.getElementById('mockimg');
  var histo = document.getElementById('histog');
  var ms    = document.getElementById('mockms');
  if (!track || !thumb || !fill || !val || !img) return;

  var EV_MIN = -5, EV_MAX = 5, EV_HOME = 2.6;
  var ev = EV_HOME;
  var raf = null, lastMs = 0;

  function fmt(v) {
    var s = Math.abs(v).toFixed(2);
    return (v < 0 ? '−' : '') + s + ' EV';
  }
  function render(now) {
    var pct = 50 + ev * 10;
    thumb.style.left = pct + '%';
    if (ev >= 0) { fill.style.left = '50%'; fill.style.width = (pct - 50) + '%'; }
    else { fill.style.left = pct + '%'; fill.style.width = (50 - pct) + '%'; }
    val.textContent = fmt(ev);
    val.classList.toggle('sl__v--hot', Math.abs(ev) > 0.005);
    var b = Math.pow(2, (ev - EV_HOME) * 0.30);
    img.style.filter = 'brightness(' + Math.min(1.9, Math.max(0.18, b)).toFixed(3) + ')';
    if (histo) histo.setAttribute('transform', 'translate(' + ((ev - EV_HOME) * 4).toFixed(1) + ' 0)');
    track.setAttribute('aria-valuenow', ev.toFixed(2));
    track.setAttribute('aria-valuetext', fmt(ev));
    if (ms && now && now - lastMs > 90) { /* the app's frame readout, alive under drag */
      ms.textContent = (8.9 + Math.random() * 0.8).toFixed(1) + ' ms';
      lastMs = now;
    }
  }

  function stopTween() { if (raf) { cancelAnimationFrame(raf); raf = null; } }

  function tweenTo(target, duration, done) {
    stopTween();
    var from = ev, t0 = null;
    function step(now) {
      if (t0 === null) t0 = now;
      var t = Math.min(1, (now - t0) / duration);
      var e = 1 - Math.pow(1 - t, 3); /* settles rather than snaps */
      ev = from + (target - from) * e;
      render(now);
      if (t < 1) raf = requestAnimationFrame(step);
      else { raf = null; if (done) done(); }
    }
    raf = requestAnimationFrame(step);
  }

  /* the one orchestrated moment: dim, breathe, then develop the frame */
  if (animOK) {
    ev = -1.2; /* start under-developed */
    render();
    setTimeout(function () {
      tweenTo(EV_HOME, 1700, function () { if (ms) ms.textContent = '9.3 ms'; });
    }, 1300);
  }

  /* yours now: pointer */
  track.addEventListener('pointerdown', function (e) {
    stopTween();
    try { track.setPointerCapture(e.pointerId); } catch (err) {}
    function move(x) {
      var r = track.getBoundingClientRect();
      var pct = Math.min(1, Math.max(0, (x - r.left) / r.width));
      ev = Math.round((EV_MIN + pct * (EV_MAX - EV_MIN)) * 100) / 100;
      render(performance.now());
    }
    move(e.clientX);
    function onMove(ev2) { move(ev2.clientX); }
    function onUp() {
      track.removeEventListener('pointermove', onMove);
      track.removeEventListener('pointerup', onUp);
      track.removeEventListener('pointercancel', onUp);
      if (ms) ms.textContent = '9.3 ms';
    }
    track.addEventListener('pointermove', onMove);
    track.addEventListener('pointerup', onUp);
    track.addEventListener('pointercancel', onUp);
    e.preventDefault();
  });

  /* and keyboard */
  track.addEventListener('keydown', function (e) {
    var step = { ArrowLeft: -0.1, ArrowRight: 0.1, ArrowDown: -0.1, ArrowUp: 0.1, PageDown: -1, PageUp: 1 }[e.key];
    if (step !== undefined) {
      stopTween();
      ev = Math.min(EV_MAX, Math.max(EV_MIN, Math.round((ev + step) * 100) / 100));
      render(performance.now());
      e.preventDefault();
    } else if (e.key === 'Home') { stopTween(); ev = EV_MIN; render(); e.preventDefault(); }
    else if (e.key === 'End') { stopTween(); ev = EV_MAX; render(); e.preventDefault(); }
  });
})();
