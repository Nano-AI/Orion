/*  Reveals, and two scrubbed scenes. Still no library: the whole job is
 *  "add a class when a block arrives" plus "map scroll position to a filter
 *  and a transform", and both fit in less code than an animation runtime.
 *
 *  The contract with the CSS: the static page IS the finished page — the
 *  developed photograph, the placed mask, the final numbers in the HTML.
 *  This script REWINDS it (html.scrub) and hands the develop back to the
 *  visitor's scroll. So with no JavaScript, or with reduced motion, nothing
 *  here runs and nothing is missing.
 */
(function () {
  'use strict';

  var doc = document.documentElement;

  // The CSS hides revealables only under html.js, so a browser that never
  // runs this file shows the whole page. Adding the class is a promise to
  // reveal everything below — hence the failsafes.
  doc.classList.add('js');

  var reduce = window.matchMedia
    && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  /* ---------- Reveals ---------- */

  var els = document.querySelectorAll('.rv, [data-rv]');

  // If the observer is unavailable, or anything below throws, the hiding
  // must not survive — a page that is permanently blank is far worse than
  // one that simply does not animate.
  function showAll() {
    for (var i = 0; i < els.length; i++) els[i].classList.add('in');
  }

  if (reduce) { showAll(); return; }   // the reduced-motion CSS does the rest
  if (!('IntersectionObserver' in window)) { showAll(); return; }

  try {
    var io = new IntersectionObserver(function (entries) {
      for (var i = 0; i < entries.length; i++) {
        if (!entries[i].isIntersecting) continue;
        entries[i].target.classList.add('in');
        io.unobserve(entries[i].target);   // reveal once, never re-hide
      }
    }, {
      // Fire a little before the block reaches the viewport, so the
      // transition is finishing as it arrives rather than starting.
      rootMargin: '0px 0px -12% 0px',
      threshold: 0.08
    });

    for (var i = 0; i < els.length; i++) io.observe(els[i]);

    // Anything already on screen at load — the hero — should not wait for a
    // scroll event that may never come.
    requestAnimationFrame(function () {
      for (var j = 0; j < els.length; j++) {
        var r = els[j].getBoundingClientRect();
        if (r.top < window.innerHeight) els[j].classList.add('in');
      }
    });

    // Failsafe. If a block is still hidden well after load — an observer
    // that never fires, a browser quirk, a print stylesheet — show
    // everything rather than leave content invisible.
    window.setTimeout(function () {
      for (var k = 0; k < els.length; k++) els[k].classList.add('in');
    }, 4000);
  } catch (e) {
    showAll();
    return;
  }

  /* ---------- The scrubbed scenes ---------- */

  // Everything below is optional garnish; if any of it throws, the page is
  // already fully readable, so fail by leaving the finished state alone.
  try {
    var devWrap  = document.getElementById('dev');
    var devImg   = document.getElementById('devImg');
    var devFill  = document.getElementById('devFill');
    var devThumb = document.getElementById('devThumb');
    var devCue   = document.getElementById('devCue');
    var hExp = document.getElementById('hExp');
    var hCon = document.getElementById('hCon');
    var hTmp = document.getElementById('hTmp');

    var maskWrap  = document.getElementById('masks');
    var maskSweep = document.getElementById('maskSweep');
    var hMask     = document.getElementById('hMask');

    if (!devWrap || !devImg || !maskWrap || !maskSweep) return;

    // Rewind. From here on the scroll owns the develop.
    doc.classList.add('scrub');

    var MINUS = '−';

    function clamp01(x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
    function easeOut(x)  { return 1 - Math.pow(1 - x, 3); }
    function easeInOut(x) { return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2; }

    // Progress through a pinned wrapper: 0 when its top reaches the top of
    // the viewport, 1 when its bottom leaves the pin.
    function progress(el) {
      var r = el.getBoundingClientRect();
      var span = r.height - window.innerHeight;
      return span > 0 ? clamp01(-r.top / span) : 1;
    }

    // Write text only when it changed; transforms and filters are cheap per
    // frame, DOM text is not.
    function setText(el, s) { if (el && el.textContent !== s) el.textContent = s; }

    function drawDev(p) {
      // The grade lands a little before the pin releases, so the finished
      // frame gets a beat on screen before the page moves on.
      var e = easeOut(clamp01(p / 0.85));
      devImg.style.filter = e >= 1 ? '' :
        'contrast(' + (0.72 + 0.28 * e).toFixed(4) +
        ') saturate(' + (0.28 + 0.72 * e).toFixed(4) +
        ') brightness(' + (1.16 - 0.16 * e).toFixed(4) + ')';

      // The real values from the real edit of this photograph — the same
      // numbers the Local panel shows in the interface screenshot below.
      setText(hExp, '+' + (2.60 * e).toFixed(2));
      setText(hCon, (1 + 0.45 * e).toFixed(2));
      setText(hTmp, String(Math.round(5500 - 1865 * e)));

      if (devFill)  devFill.style.height = (e * 100).toFixed(2) + '%';
      if (devThumb) devThumb.style.top   = (e * 100).toFixed(2) + '%';
      if (devCue)   devCue.style.opacity = String(1 - clamp01(p * 5));
    }

    function drawMask(p) {
      var e = easeInOut(clamp01(p / 0.9));
      maskSweep.style.transform =
        'translate3d(0, ' + (e * 55).toFixed(3) + '%, 0) rotate(-4deg)';
      var v = -(1.6 * e);
      setText(hMask, (v === 0 ? '0.00' : v.toFixed(2)).replace('-', MINUS));
    }

    var queued = false;
    function frame() {
      queued = false;
      drawDev(progress(devWrap));
      drawMask(progress(maskWrap));
    }
    function schedule() {
      if (!queued) { queued = true; requestAnimationFrame(frame); }
    }

    window.addEventListener('scroll', schedule, { passive: true });
    window.addEventListener('resize', schedule);
    schedule();
  } catch (e) {
    /* finished state already on screen */
  }
})();
