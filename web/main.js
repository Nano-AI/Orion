/* Orion landing site — no dependencies, no build step.
   Two jobs: remember the theme, and let the hero readouts settle the way the
   app's own numeric fields do while a slider is moving. */

(function () {
  'use strict';

  var root = document.documentElement;
  var btn = document.getElementById('themebtn');
  var label = document.getElementById('themelabel');
  var reduced = window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---------------------------------------------------------- theme --- */

  function store(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* file:// or private mode */ } }
  function load(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }

  function apply(theme) {
    root.setAttribute('data-theme', theme);
    if (label) label.textContent = theme === 'dark' ? 'Dark' : 'Light';
    if (btn) btn.setAttribute('aria-label', 'Switch to ' + (theme === 'dark' ? 'light' : 'dark') + ' theme');
  }

  var saved = load('orion-theme');
  var system = window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
  apply(saved === 'light' || saved === 'dark' ? saved : system);

  if (btn) {
    btn.addEventListener('click', function () {
      var next = root.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
      apply(next);
      store('orion-theme', next);
    });
  }

  /* Follow the system if the reader never chose for themselves. */
  window.matchMedia('(prefers-color-scheme: light)').addEventListener('change', function (e) {
    if (!load('orion-theme')) apply(e.matches ? 'light' : 'dark');
  });

  /* ------------------------------------------------------- readouts --- */
  /* The markup already carries the final value, so this only ever runs on
     top of a page that is correct without it. */

  if (reduced) return;

  var fields = Array.prototype.slice.call(document.querySelectorAll('.num'));
  if (!fields.length) return;

  function format(v, dp, sep) {
    var s = v.toFixed(dp);
    if (!sep) return s;
    var parts = s.split('.');
    parts[0] = parts[0].replace(/\B(?=(\d{3})+(?!\d))/g, ',');
    return parts.join('.');
  }

  var start = null;
  var DURATION = 620;

  function frame(now) {
    if (start === null) start = now;
    var t = Math.min(1, (now - start) / DURATION);
    var e = 1 - Math.pow(1 - t, 3);          /* ease out, settles rather than snaps */
    for (var i = 0; i < fields.length; i++) {
      var f = fields[i];
      var to = parseFloat(f.getAttribute('data-to'));
      var dp = parseInt(f.getAttribute('data-dp'), 10) || 0;
      var sep = f.getAttribute('data-sep') === '1';
      f.textContent = format(to * e, dp, sep);
    }
    if (t < 1) requestAnimationFrame(frame);
  }

  /* Only once the panel is on screen; on a phone the hero may already be past. */
  var panel = document.querySelector('.panel');
  function run() { requestAnimationFrame(frame); }

  if ('IntersectionObserver' in window && panel) {
    var io = new IntersectionObserver(function (entries) {
      if (entries[0].isIntersecting) { io.disconnect(); run(); }
    }, { threshold: 0.25 });
    io.observe(panel);
  } else {
    run();
  }
})();
