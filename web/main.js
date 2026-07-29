/*  Scroll reveals, and nothing else.
 *
 *  There is deliberately no interactivity on this page — no sliders, no
 *  toggles, no theme switch. The only JavaScript is the observer that fades
 *  each block in as it arrives, which is motion the visitor watches rather than
 *  operates.
 *
 *  Written against IntersectionObserver rather than a library: the whole job is
 *  "add a class when this scrolls into view", and shipping an animation runtime
 *  to do that would be more bytes than the rest of the page.
 */
(function () {
  'use strict';

  var els = document.querySelectorAll('.rv');

  // The CSS hides .rv until it is revealed. If the observer is unavailable, or
  // if anything below throws, that hiding must not survive — a page that is
  // permanently blank is far worse than one that simply does not animate.
  function showAll() {
    document.documentElement.classList.add('no-js');
  }

  if (!('IntersectionObserver' in window)) { showAll(); return; }

  try {
    var io = new IntersectionObserver(function (entries) {
      for (var i = 0; i < entries.length; i++) {
        if (!entries[i].isIntersecting) continue;
        entries[i].target.classList.add('in');
        io.unobserve(entries[i].target);   // reveal once, never re-hide
      }
    }, {
      // Fire a little before the block reaches the viewport, so the transition
      // is finishing as it arrives rather than starting.
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

    // Failsafe. If for any reason a block is still hidden well after load —
    // an observer that never fires, a browser quirk, a print stylesheet — show
    // everything rather than leave content invisible.
    window.setTimeout(function () {
      for (var k = 0; k < els.length; k++) els[k].classList.add('in');
    }, 4000);
  } catch (e) {
    showAll();
  }
})();
