/*  Reveals, and four scrubbed scenes. Still no library: the whole job is
 *  "add a class when a block arrives" plus "map scroll position to a few
 *  styles", and both fit in less code than an animation runtime.
 *
 *  The contract with the CSS: the static page IS the finished page — the
 *  developed photograph, the lit statement, the sharp frame, the placed
 *  mask, the final numbers in the HTML. This script REWINDS it (html.scrub)
 *  and hands the develop back to the visitor's scroll. So with no
 *  JavaScript, or with reduced motion, nothing here runs and nothing is
 *  missing.
 *
 *  Every scrubbed value is smoothed toward its target each frame rather
 *  than snapped, so a stepping mouse wheel reads as one continuous motion.
 *  The loop runs only while something is still settling.
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

  // Numbers that count up the first time they appear. The HTML holds the
  // final string, so the worst failure is a number that was simply always
  // right.
  function countUp(b) {
    if (b.getAttribute('data-counted')) return;
    b.setAttribute('data-counted', '1');
    var target = parseInt(b.getAttribute('data-count'), 10);
    var comma = b.hasAttribute('data-comma');
    var done = b.textContent;
    var t0 = performance.now();
    (function step(t) {
      var p = Math.min(1, (t - t0) / 900);
      var e = 1 - Math.pow(1 - p, 3);
      var v = Math.round(target * e);
      b.textContent = comma ? v.toLocaleString('en-US') : String(v);
      if (p < 1) requestAnimationFrame(step); else b.textContent = done;
    })(t0);
  }

  try {
    var io = new IntersectionObserver(function (entries) {
      for (var i = 0; i < entries.length; i++) {
        if (!entries[i].isIntersecting) continue;
        var el = entries[i].target;
        el.classList.add('in');
        var counts = el.querySelectorAll('[data-count]');
        for (var c = 0; c < counts.length; c++) countUp(counts[c]);
        io.unobserve(el);   // reveal once, never re-hide
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

    // Failsafe. If a block is still hidden well after load, show everything
    // rather than leave content invisible.
    window.setTimeout(showAll, 4000);
  } catch (e) {
    showAll();
    return;
  }

  /* ---------- The scrubbed scenes ---------- */

  // Everything below is optional garnish; if any of it throws, the page is
  // already fully readable, so fail by leaving the finished state alone.
  try {
    var q = function (id) { return document.getElementById(id); };

    var devWrap = q('dev'), devImg = q('devImg'), devZoom = q('devZoom'),
        devFill = q('devFill'), devThumb = q('devThumb'), devCue = q('devCue'),
        vf = q('vf'), devEye = q('devEye'), vfCenter = q('vfCenter'),
        hExp = q('hExp'), hCon = q('hCon'), hTmp = q('hTmp');
    var sayWrap = q('say'), sayText = q('sayText');
    var wipeWrap = q('speed'), wipeProxy = q('wipeProxy'), wipeEdge = q('wipeEdge');
    var maskWrap = q('masks'), maskSweep = q('maskSweep'), hMask = q('hMask');
    var proof = q('proofShot');
    var hud = q('frameHud'), hudTxt = q('frameTxt');

    if (!devWrap || !devImg || !wipeWrap || !maskWrap) return;

    // Rewind. From here on the scroll owns the develop.
    doc.classList.add('scrub');

    var MINUS = '−';

    function clamp01(x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
    function easeOut(x) { return 1 - Math.pow(1 - x, 3); }
    function easeInOut(x) { return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2; }
    function setText(el, s) { if (el && el.textContent !== s) el.textContent = s; }

    // Progress through a pinned wrapper: 0 when its top reaches the top of
    // the viewport, 1 when its bottom leaves the pin.
    function progress(el) {
      var r = el.getBoundingClientRect();
      var span = r.height - window.innerHeight;
      return span > 0 ? clamp01(-r.top / span) : 1;
    }

    // The statement's words, wrapped so they can be lit one at a time.
    // Wrapping happens only here — no script, no spans, fully lit line.
    function wrapWords(node) {
      var kids = Array.prototype.slice.call(node.childNodes);
      for (var i = 0; i < kids.length; i++) {
        var n = kids[i];
        if (n.nodeType === 3) {
          var frag = document.createDocumentFragment();
          var parts = n.textContent.split(/(\s+)/);
          for (var p = 0; p < parts.length; p++) {
            if (!parts[p]) continue;
            if (/^\s+$/.test(parts[p])) {
              frag.appendChild(document.createTextNode(parts[p]));
            } else {
              var s = document.createElement('span');
              s.className = 'w';
              s.textContent = parts[p];
              frag.appendChild(s);
            }
          }
          node.replaceChild(frag, n);
        } else if (n.nodeType === 1) {
          wrapWords(n);
        }
      }
    }

    var words = [];
    if (sayText) { wrapWords(sayText); words = sayText.querySelectorAll('.w'); }
    var lit = 0;

    // One smoothed value per scene: current chases target, so a stepping
    // wheel reads as one continuous motion.
    function S() { return { c: 0, t: 0 }; }
    var sm = { dev: S(), say: S(), wipe: S(), mask: S(), proof: S() };

    var plx = [];
    var plxEls = document.querySelectorAll('[data-plx]');
    for (var pi = 0; pi < plxEls.length; pi++) {
      plx.push({ el: plxEls[pi], box: plxEls[pi].parentNode, v: S() });
    }

    function drawDev(p) {
      // Four beats: the eye approaches the ocular and it opens to the full
      // frame; the finder wakes; the raw develops and the AF point locks;
      // then the visitor pushes through — the overlay scales past the eye
      // and fades while the photograph stays.
      var a = easeOut(clamp01(p / 0.22));
      if (devEye) {
        if (a >= 0.999) {
          devEye.style.clipPath = '';
        } else {
          // The closed ocular is a 3:2 eyepiece whatever the screen is —
          // sized from the viewport, not fixed percentages, so a phone
          // gets a window and not a slit.
          var vw = window.innerWidth, vh = window.innerHeight;
          var hi = vw < 700 ? 27 : 36;
          var wpx = vw * (1 - 2 * hi / 100);
          var hpx = Math.min(vh * 0.36, wpx * 0.66);
          var vi = (1 - hpx / vh) / 2 * 100;
          devEye.style.clipPath =
            'inset(' + (vi * (1 - a)).toFixed(3) + '% ' + (hi * (1 - a)).toFixed(3) +
            '% round ' + (28 * (1 - a)).toFixed(2) + 'px)';
        }
      }

      var e = easeOut(clamp01((p - 0.20) / 0.42));
      devImg.style.filter = e >= 0.999 ? '' :
        'contrast(' + (0.72 + 0.28 * e).toFixed(4) +
        ') saturate(' + (0.28 + 0.72 * e).toFixed(4) +
        ') brightness(' + (1.16 - 0.16 * e).toFixed(4) + ')';

      // A slight dolly on the approach, then the frame settles home as the
      // grade lands.
      if (devZoom) {
        var zs = 1.09 - 0.09 * e + 0.10 * (1 - a);
        devZoom.style.transform = (a >= 0.999 && e >= 0.999) ? '' :
          'scale(' + zs.toFixed(4) + ')';
      }

      // The real values from the real edit of this photograph — the same
      // numbers the Local panel shows in the interface screenshot below.
      setText(hExp, '+' + (2.60 * e).toFixed(2));
      setText(hCon, (1 + 0.45 * e).toFixed(2));
      setText(hTmp, String(Math.round(5500 - 1865 * e)));

      if (devFill) devFill.style.height = (e * 100).toFixed(2) + '%';
      if (devThumb) devThumb.style.top = (e * 100).toFixed(2) + '%';
      if (devCue) devCue.style.opacity = String(1 - clamp01((p - 0.02) * 4));

      var push = clamp01((p - 0.76) / 0.24);
      push = push * push;
      var exitT = push <= 0 ? '' : 'scale(' + (1 + 0.85 * push).toFixed(4) + ')';
      if (vf) {
        // Wakes as the ocular reaches the frame, leaves on the push.
        var fo = Math.min(clamp01((p - 0.15) / 0.09), 1 - push);
        vf.style.opacity = fo.toFixed(4);
        vf.style.transform = exitT;
        vf.classList.toggle('lock', e >= 0.999);
      }
      if (vfCenter) {
        vfCenter.style.transform = exitT;
        vfCenter.style.opacity = push <= 0 ? '' : (1 - push).toFixed(4);
      }
    }

    function drawSay(p) {
      if (!words.length) return;
      var e = clamp01((p - 0.08) / 0.72);
      var n = Math.round(e * words.length);
      while (lit < n) { words[lit].classList.add('on'); lit++; }
      while (lit > n) { lit--; words[lit].classList.remove('on'); }
    }

    function drawWipe(p) {
      var e = easeInOut(clamp01(p / 0.9));
      var x = e * 103;   // past 100 so the sweep line exits the frame
      if (wipeProxy) wipeProxy.style.clipPath = 'inset(0 0 0 ' + Math.min(x, 100).toFixed(3) + '%)';
      if (wipeEdge) {
        wipeEdge.style.transform = 'translate3d(' + x.toFixed(3) + '%, 0, 0)';
        wipeEdge.style.opacity = String(clamp01((103 - x) / 6));
      }
    }

    function drawMask(p) {
      var e = easeInOut(clamp01(p / 0.9));
      maskSweep.style.transform =
        'translate3d(0, ' + (e * 55).toFixed(3) + '%, 0) rotate(-4deg)';
      var v = -(1.6 * e);
      setText(hMask, (v === 0 ? '0.00' : v.toFixed(2)).replace('-', MINUS));
    }

    function drawProof(p) {
      if (!proof) return;
      var e = easeOut(p);
      proof.style.transform = e >= 0.999 ? '' :
        'rotateX(' + ((1 - e) * 10).toFixed(3) + 'deg)' +
        ' translateY(' + ((1 - e) * 44).toFixed(2) + 'px)' +
        ' scale(' + (0.94 + 0.06 * e).toFixed(4) + ')';
    }

    function drawPlx() {
      for (var i = 0; i < plx.length; i++) {
        plx[i].el.style.transform =
          'translate3d(0, ' + (plx[i].v.c * -52).toFixed(2) + 'px, 0)';
      }
    }

    // The frame counter follows whichever section crosses mid-viewport.
    var hudLabel = '';
    function setFrame(label) {
      if (!hud || label === hudLabel) return;
      hudLabel = label;
      setText(hudTxt, label);
      hud.classList.remove('tick');
      void hud.offsetWidth;                    // restart the tick animation
      hud.classList.add('tick');
    }
    var fio = new IntersectionObserver(function (entries) {
      for (var i = 0; i < entries.length; i++) {
        if (entries[i].isIntersecting) {
          setFrame(entries[i].target.getAttribute('data-frame'));
        }
      }
    }, { rootMargin: '-42% 0px -42% 0px', threshold: 0 });
    var framed = document.querySelectorAll('[data-frame]');
    for (var fi = 0; fi < framed.length; fi++) fio.observe(framed[fi]);

    /* ---------- The loop ---------- */

    var running = false;
    var lastScroll = 0;

    function frame(now) {
      var vh = window.innerHeight;

      // Read every target in one batch, before any style writes.
      sm.dev.t = progress(devWrap);
      sm.say.t = sayWrap ? progress(sayWrap) : 0;
      sm.wipe.t = progress(wipeWrap);
      sm.mask.t = progress(maskWrap);
      if (proof) {
        var pr = proof.getBoundingClientRect();
        sm.proof.t = clamp01((vh * 0.96 - pr.top) / (vh * 0.55));
      }
      for (var i = 0; i < plx.length; i++) {
        var br = plx[i].box.getBoundingClientRect();
        var c = (br.top + br.height / 2 - vh / 2) / vh;
        plx[i].v.t = c < -1 ? -1 : c > 1 ? 1 : c;
      }
      var devBottom = devWrap.getBoundingClientRect().bottom;

      // Chase. 0.16 settles in ~0.3 s — present, never syrupy.
      var settling = false;
      function chase(s) {
        var d = s.t - s.c;
        if (Math.abs(d) > 0.0004) { s.c += d * 0.16; settling = true; }
        else s.c = s.t;
      }
      chase(sm.dev); chase(sm.say); chase(sm.wipe); chase(sm.mask); chase(sm.proof);
      for (var j = 0; j < plx.length; j++) chase(plx[j].v);

      drawDev(sm.dev.c);
      drawSay(sm.say.c);
      drawWipe(sm.wipe.c);
      drawMask(sm.mask.c);
      drawProof(sm.proof.c);
      drawPlx();

      if (hud) hud.classList.toggle('fr--on', devBottom < vh * 0.4);

      if (settling || now - lastScroll < 200) {
        requestAnimationFrame(frame);
      } else {
        running = false;
      }
    }

    function wake() {
      lastScroll = performance.now();
      if (!running) { running = true; requestAnimationFrame(frame); }
    }

    window.addEventListener('scroll', wake, { passive: true });
    window.addEventListener('resize', wake);

    // The first frame runs synchronously so the ocular is closed before
    // the page ever paints — no flash of the full photograph.
    lastScroll = performance.now();
    running = true;
    frame(lastScroll);
  } catch (e) {
    /* finished state already on screen */
  }
})();
