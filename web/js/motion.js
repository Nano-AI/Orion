/*  Orion site: the motion layer. GSAP + ScrollTrigger for choreography,
 *  Lenis for the scroll itself.
 *
 *  The HTML and CSS already hold every section's FINISHED state, so no-JS,
 *  reduced motion, a missing vendor file or any thrown error all leave the
 *  complete page. Each moment that moves shows a change: the hero develops,
 *  the mask lands on the app, an exposure drag sweeps seven renders, the
 *  papers turn, the assistant's proposal slides over yours while the calls
 *  run, and the pier flies in and develops. The sky is the one thing here that is only
 *  decoration.
 */
(() => {
  'use strict';
  const { gsap, ScrollTrigger, Lenis } = window;
  if (!gsap || !ScrollTrigger || location.search.includes('nomotion')) return;
  if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;

  gsap.registerPlugin(ScrollTrigger);
  ScrollTrigger.config({ ignoreMobileResize: true });
  document.documentElement.classList.add('motion');
  const EASE = 'expo.out';
  const NAV = 78;   // the floating nav (--nav-h): pinned sections centre in the space under it
  const mm = gsap.matchMedia();

  // ---------- Lenis, driven by GSAP's ticker so ScrollTrigger reads one clock ----------
  if (Lenis) {
    const lenis = new Lenis({ lerp: 0.11, anchors: { offset: -NAV } });
    window.__lenis = lenis;
    lenis.on('scroll', ScrollTrigger.update);
    gsap.ticker.add(t => lenis.raf(t * 1000));
    gsap.ticker.lagSmoothing(0);
  }

  const q = (s, r = document) => [...r.querySelectorAll(s)];
  const guard = (name, fn) => { try { fn(); } catch (e) { console.warn('[orion motion]', name, e); } };
  // The sky's presence and warp, set by the hero's lift-off and the close's re-entry. vis 0 is no
  // sky (over both photographs, and nothing is drawn); warp 1 is the stars rushing past.
  const sky = { vis: 0, warp: 1 };

  // Moves a slider figure the same way a hand does, unless a hand already has.
  const drive = (fig, value) => {
    const input = fig.querySelector('input[type="range"]');
    if (!input || fig.dataset.touched) return;
    input.value = value;
    input.dispatchEvent(new Event('input'));
  };

  // ---------- the sky: the Milky Way, stars at three depths, planets nearer in, Orion at the end ----------
  // A generated Milky Way sits farthest back. The stars are scattered, not charted, in layers that move at different speeds, with dust
  // nearer still and passing in front of the planets. The planets are texture-mapped spheres
  // with real axial tilts; the nearer (larger) a planet, the faster it moves, and the farthest
  // is softened. Before the close Orion rises to the middle of the screen: its stars at their
  // SIMBAD positions, joined as d3-celestial joins them, the belt lit and named; the close flies in
  // out of the belt. The hero lifts off into the sky and the close re-enters out of it (sky, below).
  const ORION = [   // J2000 right ascension and declination, degrees, and V magnitude (SIMBAD; d3-celestial's Hipparcos stars agree)
    { ra: 88.7929, dec: 7.4071, mag: 0.45, color: '#ffd3ae' },   // 0 Betelgeuse
    { ra: 81.2828, dec: 6.3497, mag: 1.64 },                     // 1 Bellatrix
    { ra: 83.7845, dec: 9.9342, mag: 3.39 },                     // 2 Meissa, the head
    { ra: 85.1897, dec: -1.9426, mag: 1.74, belt: true },        // 3 Alnitak
    { ra: 84.0534, dec: -1.2019, mag: 1.69, belt: true },        // 4 Alnilam
    { ra: 83.0017, dec: -0.2991, mag: 2.25, belt: true },        // 5 Mintaka
    { ra: 81.1192, dec: -2.3971, mag: 3.35 },                    // 6 eta Orionis
    { ra: 86.9391, dec: -9.6696, mag: 2.07 },                    // 7 Saiph
    { ra: 78.6345, dec: -8.2016, mag: 0.18 },                    // 8 Rigel
  ];
  // Orion's body as d3-celestial's constellations.lines.json draws it, by index above (club and shield left out)
  const FIGURE = [[0, 1], [1, 2], [2, 0], [0, 3], [1, 5], [3, 4], [4, 5], [3, 7], [5, 6], [6, 8]];
  const LAYERS = [
    // per: one star per this many square pixels. Farther layers are smaller, fainter, bluer and slower.
    { per: 5200, r: [0.3, 0.7], a: [0.12, 0.35], speed: 0.03, color: '#c8d4ff' },
    { per: 16000, r: [0.5, 1], a: [0.3, 0.6], speed: 0.09, color: '#eceef2' },
    { per: 60000, r: [0.9, 1.5], a: [0.5, 0.85], speed: 0.18, color: '#fff4e6' },
  ];
  const DUST = { per: 30000, r: [0.5, 1.4], a: [0.04, 0.14], speed: 0.4, color: '#dfe6ea' };
  const PLANETS = [
    // Farthest first, so nearer planets pass in front. anchor: the section a planet belongs to; ry: where
    // it sits, as a share of the screen height, when that section's top reaches the top of the screen;
    // x: its centre across the screen; size: diameter as a share of the width; depth: how fast it moves
    // against the page, larger for nearer (bigger) planets; roll and tilt: the spin axis, leaned in the
    // picture and toward us, degrees (Saturn's ring shares them); soft: drawn from a fraction of the
    // pixels and scaled up, for distance (a canvas blur filter costs a frame).
    { map: 'mars', anchor: '#register', ry: 0.24, x: 0.86, size: 0.05, depth: 0.18, spin: 80, roll: -22, tilt: 18, glow: [255, 160, 120], soft: 0.5 },
    { map: 'saturn', anchor: '#keep', ry: 0.55, x: 0.9, size: 0.1, depth: 0.3, spin: 60, roll: -18, tilt: 24, glow: [255, 230, 196], ring: true },
    { map: 'jupiter', anchor: '#research', ry: 1.2, x: 0.03, size: 0.3, depth: 0.6, spin: 45, roll: 4, tilt: 10, glow: [255, 222, 186] },
  ];
  // Saturn's rings: 48 samples across Solar System Scope's ring map (see NOTICE), from the C ring's
  // inner edge at 1.24 planet radii to the A ring's outer edge at 2.27; the gap two thirds out is Cassini's
  const RING = [[255,255,255,0.00],[255,255,255,0.00],[229,227,254,0.02],[36,28,78,0.11],[52,49,86,0.21],[70,64,89,0.26],[83,73,95,0.33],[85,77,99,0.39],[89,79,101,0.41],[91,83,102,0.41],[93,86,104,0.45],[91,84,102,0.43],[86,80,100,0.42],[95,89,105,0.46],[117,108,116,0.58],[158,145,145,0.79],[162,151,147,0.81],[160,150,148,0.81],[166,153,151,0.83],[173,160,157,0.86],[201,183,175,0.93],[206,189,183,0.94],[201,189,187,0.94],[201,181,167,0.93],[207,189,170,0.94],[218,202,186,0.95],[215,201,189,0.95],[197,183,167,0.92],[197,183,170,0.91],[200,186,176,0.92],[195,182,172,0.91],[197,187,187,0.93],[96,96,105,0.45],[78,81,94,0.32],[104,104,114,0.56],[139,136,141,0.78],[148,142,144,0.80],[144,137,133,0.71],[143,137,130,0.69],[142,135,130,0.69],[142,134,130,0.69],[143,136,131,0.70],[135,130,126,0.66],[137,134,130,0.71],[145,144,145,0.81],[45,42,95,0.17],[25,24,91,0.03],[24,24,83,0.05]];
  guard('sky', () => {
    const D2R = Math.PI / 180;
    const canvas = document.createElement('canvas');
    canvas.className = 'stars';
    canvas.setAttribute('aria-hidden', 'true');
    document.body.prepend(canvas);
    const ctx = canvas.getContext('2d');
    let w = 0, h = 0, dpr = 1, frame = 0, layers = [], dust = [];
    const scatter = L => Array.from({ length: Math.round((w * h) / L.per) }, () => ({
      x: Math.random() * w, y: Math.random() * h,
      r: L.r[0] + Math.random() * (L.r[1] - L.r[0]), a: L.a[0] + Math.random() * (L.a[1] - L.a[0]), phase: Math.random() * 6.28,
    }));
    const drawField = (L, pts, sy, time) => {
      // the warp spreads a field out from the centre, nearer layers faster: stars rushing past
      const z = 1 + sky.warp * (1 + L.speed * 14), cx = w / 2, cy = h / 2;
      ctx.fillStyle = L.color;
      for (const s of pts) {
        const x = cx + (s.x - cx) * z, y = cy + (((((s.y - sy * L.speed) % h) + h) % h) - cy) * z;
        if (x < -3 || x > w + 3 || y < -3 || y > h + 3) continue;
        ctx.globalAlpha = s.a * (0.8 + 0.2 * Math.sin(time * 1.3 + s.phase));
        ctx.beginPath(); ctx.arc(x, y, s.r * Math.min(z, 2.5), 0, 6.283); ctx.fill();
      }
      ctx.globalAlpha = 1;
    };

    // --- the Milky Way: a band of light with dark dust lanes along its middle and stars crowding into
    // it, generated once at low resolution after the page loads, drawn large and soft behind the stars,
    // and turned a little as the page scrolls ---
    const hash = (x, y) => {
      let n = Math.imul(x, 374761393) + Math.imul(y, 668265263);
      n = Math.imul(n ^ (n >>> 13), 1274126177);
      return ((n ^ (n >>> 16)) >>> 0) / 4294967295;
    };
    const vnoise = (x, y) => {
      const xi = Math.floor(x), yi = Math.floor(y), tx = x - xi, ty = y - yi;
      const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
      const a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
      return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
    };
    const fbm = (x, y) => {
      let v = 0, amp = 0.5;
      for (let o = 0; o < 4; o++) { v += amp * vnoise(x, y); x *= 2.03; y *= 2.03; amp *= 0.5; }
      return v;
    };
    const GALAXY = 320, SLANT = -0.5;   // texture size in pixels; the band's slant, radians
    let galaxy = null;
    const bandStars = [];
    const makeGalaxy = () => {
      const c = document.createElement('canvas');
      c.width = c.height = GALAXY;
      const g = c.getContext('2d'), img = g.createImageData(GALAXY, GALAXY), cs = Math.cos(SLANT), sn = Math.sin(SLANT);
      for (let y = 0; y < GALAXY; y++) for (let x = 0; x < GALAXY; x++) {
        const u = x / GALAXY - 0.5, v = y / GALAXY - 0.5, along = u * cs + v * sn, across = v * cs - u * sn;
        const width = 0.1 + 0.05 * vnoise(along * 9 + 20, 0.5);
        let light = Math.exp(-((across / width) ** 2)) * (0.35 + 0.8 * fbm(x / 38, y / 38))   // the band, in clouds
          + 0.7 * Math.exp(-((along - 0.1) ** 2) / 0.01 - (across ** 2) / 0.003);                // the bright bulge toward the core
        const lanes = Math.max(0, Math.min(1, (fbm(x / 17 + 9, y / 17 + 3) - 0.45) / 0.22));
        light *= 1 - 0.8 * lanes * Math.exp(-((across / 0.045) ** 2));                          // dust along the middle
        const warm = Math.min(1, light), o = (y * GALAXY + x) * 4;
        img.data[o] = 118 + 137 * warm; img.data[o + 1] = 136 + 90 * warm; img.data[o + 2] = 214 - 20 * warm;
        img.data[o + 3] = 255 * 0.28 * Math.min(1, light);
        if (hash(x * 7 + 1, y * 13 + 5) < Math.min(1, light) * 0.03) bandStars.push([u, v, 0.25 + 0.6 * hash(x, y * 3)]);
      }
      g.putImageData(img, 0, 0);
      galaxy = c;
    };
    const drawGalaxy = (sy, maxScroll) => {
      if (!galaxy) return;
      const S = Math.hypot(w, h) * 1.15 * (1 + sky.warp * 0.6);   // big enough to cover the screen however it turns; larger in the warp
      ctx.save();
      ctx.translate(w / 2, h / 2 - sy * 0.015);
      ctx.rotate(-0.2 * (sy / maxScroll));
      ctx.drawImage(galaxy, -S / 2, -S / 2, S, S);
      ctx.fillStyle = '#f4f1ea';
      for (const [u, v, a] of bandStars) {
        ctx.globalAlpha = a;
        ctx.beginPath(); ctx.arc(u * S, v * S, 0.6, 0, 6.283); ctx.fill();
      }
      ctx.restore();
    };

    // --- Orion, at the end ---
    const fade = gsap.parseEase('power1.in');
    const drawOrion = sy => {
      // A far layer, like the planets: it rises at 0.3 of the page's speed and is centred on the screen,
      // the belt in the middle, as the close reaches the top. The close then flies in out of the belt,
      // and the warp throws the figure out past the edges.
      const k = Math.min(Math.max(w, 700) * 0.022, ((h - 160) * 0.8) / 19.6);   // pixels per degree; the figure is 19.6 degrees tall
      const below = Math.max(0, closeTop - sy) * 0.3;
      const e = fade(gsap.utils.clamp(0, 1, 1 - below / (h / 2 + 11.5 * k)));   // 0 while its top is still below the screen
      if (e < 0.01) return;
      const z = 1 + sky.warp * 3, cx = w / 2, cy = h / 2 + below;
      const pts = ORION.map(st => [cx - (st.ra - 84.0534) * Math.cos(1.2 * D2R) * k * z, cy - (st.dec + 1.2019) * k * z]);   // Alnilam at the centre; east is left
      const core = st => Math.max(0.9, 2.6 - 0.5 * st.mag);   // brighter is bigger
      ctx.save();
      ctx.globalAlpha = e;
      // the figure: each line stops short of its stars; the belt's two are teal
      for (const [a, b] of FIGURE) {
        const [x0, y0] = pts[a], [x1, y1] = pts[b], len = Math.hypot(x1 - x0, y1 - y0);
        const g0 = (core(ORION[a]) + 6) / len, g1 = (core(ORION[b]) + 6) / len, belt = ORION[a].belt && ORION[b].belt;
        ctx.strokeStyle = belt ? 'rgba(77, 182, 196, 0.85)' : 'rgba(236, 238, 242, 0.22)';
        ctx.lineWidth = belt ? 1.4 : 1;
        ctx.beginPath();
        ctx.moveTo(x0 + (x1 - x0) * g0, y0 + (y1 - y0) * g0);
        ctx.lineTo(x1 - (x1 - x0) * g1, y1 - (y1 - y0) * g1);
        ctx.stroke();
      }
      // the stars: a core sized by brightness in a tight glow
      ORION.forEach((st, i) => {
        const [x, y] = pts[i], r = core(st), glow = r * (st.belt ? 4.5 : 3.5);
        const g = ctx.createRadialGradient(x, y, 0, x, y, glow);
        g.addColorStop(0, st.belt ? 'rgba(126, 240, 244, 0.55)' : 'rgba(236, 238, 242, 0.3)');
        g.addColorStop(1, st.belt ? 'rgba(77, 182, 196, 0)' : 'rgba(236, 238, 242, 0)');
        ctx.fillStyle = g; ctx.beginPath(); ctx.arc(x, y, glow, 0, 6.283); ctx.fill();
        ctx.fillStyle = st.belt ? '#eafdfe' : st.color || '#e6ecff'; ctx.beginPath(); ctx.arc(x, y, r, 0, 6.283); ctx.fill();
      });
      // the belt's name carries its line on past Alnitak, clear of the lines that leave that star
      const [ax, ay] = pts[3], [mx, my] = pts[5];
      ctx.translate(ax, ay);
      ctx.rotate(Math.atan2(my - ay, mx - ax));
      ctx.font = '500 11px "Martian Mono", ui-monospace, monospace';
      if ('letterSpacing' in ctx) ctx.letterSpacing = '2px';
      ctx.textAlign = 'right'; ctx.textBaseline = 'middle'; ctx.fillStyle = '#4db6c4';
      ctx.fillText("ORION'S BELT", -14, 0);
      ctx.restore();
    };

    // --- planets: a lookup from each pixel of the disc to the map, built once per size ---
    const planets = [];
    const LIGHT = [-0.78, -0.34, 0.52];   // one sun for every planet: upper left, a little in front
    const build = (pl, D) => {
      const n = D * D, u0 = new Float32Array(n), row = new Int32Array(n), shade = new Float32Array(n), alpha = new Uint8Array(n);
      const cr = Math.cos(pl.roll * D2R), sr = Math.sin(pl.roll * D2R), cb = Math.cos(pl.tilt * D2R), sb = Math.sin(pl.tilt * D2R);
      for (let yy = 0; yy < D; yy++) for (let xx = 0; xx < D; xx++) {
        const nx = ((xx + 0.5) / D) * 2 - 1, ny = ((yy + 0.5) / D) * 2 - 1, r2 = nx * nx + ny * ny, i = yy * D + xx;
        if (r2 >= 1) continue;
        const nz = Math.sqrt(1 - r2);
        // screen to planet: undo the roll in the picture plane, then the tilt of the pole toward us
        const x1 = nx * cr + ny * sr, y1 = ny * cr - nx * sr;
        const yb = y1 * cb - nz * sb, zb = y1 * sb + nz * cb;
        u0[i] = (Math.atan2(x1, zb) / (2 * Math.PI) + 0.5) * pl.tw;
        row[i] = Math.min(pl.th - 1, Math.floor((0.5 + Math.asin(Math.max(-1, Math.min(1, yb))) / Math.PI) * pl.th)) * pl.tw;
        const lit = Math.max(0, nx * LIGHT[0] + ny * LIGHT[1] + nz * LIGHT[2]);
        shade[i] = (0.012 + 0.988 * Math.pow(lit, 0.8)) * (0.8 + 0.2 * nz);
        alpha[i] = 255 * Math.min(1, (1 - Math.sqrt(r2)) * D * 0.5);
      }
      const c = document.createElement('canvas');
      c.width = c.height = D;
      const g = c.getContext('2d');
      // a thin lit limb, the way an atmosphere catches the sun
      const HD = Math.round(D * 1.3), halo = document.createElement('canvas');
      halo.width = halo.height = HD;
      const hg = halo.getContext('2d'), hd = hg.createImageData(HD, HD);
      for (let yy = 0; yy < HD; yy++) for (let xx = 0; xx < HD; xx++) {
        const nx = ((xx + 0.5) / HD) * 2.6 - 1.3, ny = ((yy + 0.5) / HD) * 2.6 - 1.3, rr = Math.hypot(nx, ny);
        if (rr < 0.96 || rr > 1.3) continue;
        const side = Math.max(0, ((nx * LIGHT[0] + ny * LIGHT[1]) / rr) * 0.9 + 0.1), o = (yy * HD + xx) * 4;
        hd.data[o] = pl.glow[0]; hd.data[o + 1] = pl.glow[1]; hd.data[o + 2] = pl.glow[2];
        hd.data[o + 3] = 80 * side * Math.exp(-Math.max(0, rr - 1) * 16);
      }
      hg.putImageData(hd, 0, 0);
      let ring = null;
      if (pl.ring) {
        // the ring seen from above, smooth from the 48 samples; drawn squashed and rolled to match the planet's axis
        const RS = Math.min(1200, Math.round(D * 2.27));
        ring = document.createElement('canvas');
        ring.width = ring.height = RS;
        const rg = ring.getContext('2d'), rd = rg.createImageData(RS, RS);
        for (let yy = 0; yy < RS; yy++) for (let xx = 0; xx < RS; xx++) {
          const rr = Math.hypot(((xx + 0.5) / RS) * 2 - 1, ((yy + 0.5) / RS) * 2 - 1) * 2.27;   // in planet radii
          if (rr < 1.24 || rr > 2.27) continue;
          const f = ((rr - 1.24) / 1.03) * (RING.length - 1), k = Math.floor(f), t = f - k;
          const A = RING[k], B = RING[Math.min(RING.length - 1, k + 1)], o = (yy * RS + xx) * 4;
          for (let ch = 0; ch < 3; ch++) rd.data[o + ch] = A[ch] + (B[ch] - A[ch]) * t;
          rd.data[o + 3] = 255 * (A[3] + (B[3] - A[3]) * t);
        }
        rg.putImageData(rd, 0, 0);
      }
      return { D, c, g, out: g.createImageData(D, D), u0, row, shade, alpha, halo, ring };
    };
    const paint = (pl, time) => {
      const { g, out, u0, row, shade, alpha } = pl.buf, d = out.data, tex = pl.tex, tw = pl.tw;
      const shift = tw - (((time / pl.spin) * tw) % tw);   // the surface turns west to east
      for (let i = 0, o = 0; i < u0.length; i++, o += 4) {
        if (!alpha[i]) { d[o + 3] = 0; continue; }
        const t = (row[i] + (((u0[i] + shift) % tw) | 0)) * 4, s = shade[i];
        d[o] = tex[t] * s; d[o + 1] = tex[t + 1] * s; d[o + 2] = tex[t + 2] * s; d[o + 3] = alpha[i];
      }
      g.putImageData(out, 0, 0);
    };
    const drawRing = (pl, x, y, r) => {
      const R = r * 2.27, a = pl.roll * D2R;
      ctx.save();
      // One draw, so the ring has no seam: it is masked only where its far side passes behind the
      // planet, the half of the disc beyond the ring's long axis.
      ctx.beginPath();
      ctx.rect(0, 0, w, h);
      ctx.moveTo(x + Math.cos(a + Math.PI) * r, y + Math.sin(a + Math.PI) * r);
      ctx.arc(x, y, r, a + Math.PI, a + 2 * Math.PI);
      ctx.closePath();
      ctx.clip('evenodd');
      ctx.translate(x, y); ctx.rotate(a); ctx.scale(1, Math.sin(pl.tilt * D2R));
      ctx.drawImage(pl.buf.ring, -R, -R, 2 * R, 2 * R);
      ctx.restore();
    };
    const drawPlanet = (pl, time, sy) => {
      const size = pl.size * Math.max(w, 700), r = size / 2;
      // on a phone the text runs edge to edge, so each planet moves out to peek from its side
      const px = w < 700 ? (pl.x > 0.5 ? Math.max(pl.x, 0.95) : Math.min(pl.x, 0.05)) : pl.x;
      const x = px * w, y = h * pl.ry + (pl.top - sy) * pl.depth, reach = r * (pl.ring ? 2.3 : 1.3);
      if (y + reach < 0 || y - reach > h) return;
      const D = Math.min(Math.round(size * dpr * (pl.soft || 1)), 520);
      if (!pl.buf || pl.buf.D !== D) { pl.buf = build(pl, D); paint(pl, time); }
      else if (frame % 3 === 0) paint(pl, time);
      ctx.globalAlpha = (w < 700 ? 0.6 : 1) * (0.8 + 0.2 * Math.min(1, pl.depth / 0.6));   // farther is dimmer
      ctx.drawImage(pl.buf.halo, x - r * 1.3, y - r * 1.3, size * 1.3, size * 1.3);
      ctx.drawImage(pl.buf.c, x - r, y - r, size, size);
      if (pl.ring) drawRing(pl, x, y, r);
      ctx.globalAlpha = 1;
    };
    // a section's top, and the page's length, move when pins are measured: re-read them on every
    // refresh, never per frame (reading scrollHeight in the ticker forces a layout every frame)
    let maxScroll = 1, closeTop = 0;
    const anchor = () => {
      maxScroll = Math.max(1, document.documentElement.scrollHeight - innerHeight);
      const close = document.querySelector('.alpha');
      if (close) {   // where Orion centres; measured on the pin's spacer, since a pinned element reports the screen
        const box = close.parentElement.classList.contains('pin-spacer') ? close.parentElement : close;
        closeTop = box.getBoundingClientRect().top + scrollY;
      }
      planets.forEach(pl => {
        const el = document.querySelector(pl.anchor);
        pl.top = el ? el.getBoundingClientRect().top + scrollY : 0;
      });
    };
    ScrollTrigger.addEventListener('refresh', anchor);
    anchor();
    // the maps are data: URLs (js/planet-maps.js), which a canvas may read back even from file://
    const loadPlanets = () => {
      const script = document.createElement('script');
      script.src = 'js/planet-maps.js';
      script.onload = () => PLANETS.forEach((cfg, order) => {
        const img = new Image();
        img.onload = () => {
          const c = document.createElement('canvas');
          c.width = img.naturalWidth; c.height = img.naturalHeight;
          const g = c.getContext('2d');
          g.drawImage(img, 0, 0);
          planets.push({ ...cfg, order, tex: g.getImageData(0, 0, c.width, c.height).data, tw: c.width, th: c.height, buf: null, top: 0 });
          planets.sort((m, n) => m.order - n.order);
          anchor();
        };
        img.src = window.ORION_PLANET_MAPS[cfg.map];
      });
      document.head.append(script);
    };
    // both wait for the page's own images; the maps, which are a download, never come on a data saver
    const idle = fn => (window.requestIdleCallback || setTimeout)(fn);
    addEventListener('load', () => {
      idle(makeGalaxy);
      if (!(navigator.connection && navigator.connection.saveData)) idle(loadPlanets);
    });

    const size = () => {
      dpr = Math.min(devicePixelRatio || 1, 2);
      w = innerWidth; h = innerHeight;
      canvas.width = w * dpr; canvas.height = h * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      layers = LAYERS.map(scatter);
      dust = scatter(DUST);
    };
    size();
    addEventListener('resize', size);

    let streak = null, nextStreak = performance.now() + 7000;
    let shown = -1;
    gsap.ticker.add(time => {
      frame++;
      if (sky.vis !== shown) { shown = sky.vis; canvas.style.opacity = shown; }
      if (shown < 0.004) return;   // over the hero and the close there is no sky to draw
      const sy = scrollY;
      ctx.clearRect(0, 0, w, h);
      drawGalaxy(sy, maxScroll);
      LAYERS.forEach((L, i) => drawField(L, layers[i], sy, time));
      drawOrion(sy);
      for (const pl of planets) drawPlanet(pl, time, sy);
      drawField(DUST, dust, sy, 0);   // nearest of all, in front of the planets

      // one shooting star at a time, every ten to twenty seconds
      const now = performance.now();
      if (!streak && now > nextStreak && sky.warp < 0.01) {
        streak = { x: w * (0.3 + Math.random() * 0.65), y: h * Math.random() * 0.45, t0: now, len: 110 + Math.random() * 120, ang: 2.55 + Math.random() * 0.35 };
      }
      if (streak) {
        const t = (now - streak.t0) / 950;
        if (t >= 1) { streak = null; nextStreak = now + 10000 + Math.random() * 10000; }
        else {
          const cx = Math.cos(streak.ang), cy = Math.sin(streak.ang);
          const hx = streak.x + cx * t * 420, hy = streak.y + cy * t * 420;
          const g = ctx.createLinearGradient(hx, hy, hx - cx * streak.len, hy - cy * streak.len);
          g.addColorStop(0, 'rgba(236,236,238,0.95)'); g.addColorStop(1, 'rgba(236,236,238,0)');
          ctx.globalAlpha = Math.sin(t * Math.PI); ctx.strokeStyle = g; ctx.lineWidth = 1.2;
          ctx.beginPath(); ctx.moveTo(hx, hy); ctx.lineTo(hx - cx * streak.len, hy - cy * streak.len); ctx.stroke();
        }
      }
      ctx.globalAlpha = 1;
    });
  });

  // ---------- hero: the headline arrives, then scrolling develops the frame ----------
  guard('hero', () => {
    const hero = document.querySelector('.hero');
    const fig = hero && hero.querySelector('[data-compare]');
    if (!fig) return;
    // pos is where the as-shot frame ends: 100 is all as shot, 0 all developed.
    const s = { pos: 100 };
    const move = () => drive(fig, s.pos);
    move();
    const intro = gsap.timeline({ defaults: { ease: EASE } });
    intro.from('.hero__copy .wrap > *', { y: 32, opacity: 0, duration: 1.2, stagger: 0.09 }, 0.1);
    // lift-off: the photograph falls away into space, and the stars come in around it out of the warp
    gsap.set(fig, { transformPerspective: 1400 });
    const liftOff = (tl, at, span) => tl
      .to(fig, { scale: 0.42, rotationX: 26, yPercent: -6, borderRadius: 28, duration: span, ease: 'power2.inOut' }, at)
      .to(fig, { opacity: 0, duration: span * 0.4, ease: 'power1.in' }, at + span * 0.6)
      .fromTo(sky, { vis: 0, warp: 1 }, { vis: 1, warp: 0, duration: span, ease: 'power2.out', immediateRender: false }, at);
    mm.add('(min-width: 761px)', () => {
      intro.to(s, { pos: 86, duration: 1.4, onUpdate: move }, 0.8);
      const tl = gsap.timeline({ scrollTrigger: { trigger: hero, start: 'top top', end: '+=180%', pin: true, scrub: 0.8 } })
        .fromTo(s, { pos: 86 }, { pos: 0, duration: 1, ease: 'power1.inOut', onUpdate: move, immediateRender: false }, 0)
        .to('.hero__copy', { y: -64, opacity: 0, duration: 0.4, ease: 'power2.in' }, 0.05);
      liftOff(tl, 1, 0.8);
    });
    mm.add('(max-width: 760px)', () => {
      intro.to(s, { pos: 42, duration: 1.8, onUpdate: move }, 0.9);
      liftOff(gsap.timeline({ scrollTrigger: { trigger: hero, start: 'top top', end: 'bottom top', scrub: 0.6 } }), 0, 1);
    });
  });

  // ---------- entrances: once, then still ----------
  guard('reveal', () => {
    const els = q('[data-reveal]');
    try {
      gsap.set(els, { opacity: 0, y: 28 });
      ScrollTrigger.batch(els, {
        start: 'top 90%', once: true,
        onEnter: batch => gsap.to(batch, { opacity: 1, y: 0, duration: 1.1, ease: EASE, stagger: 0.08, overwrite: true }),
      });
    } catch (e) {
      gsap.set(els, { clearProps: 'opacity,transform' });
      throw e;
    }
  });

  // ---------- numbers count up once ----------
  guard('counts', () => {
    q('[data-count]').forEach(el => {
      const end = parseFloat(el.dataset.count);
      const dp = Number(el.dataset.decimals || 0);
      const finalHTML = el.innerHTML;
      const o = { v: 0 };
      gsap.to(o, {
        v: end, duration: 1.6, ease: 'power3.out',
        scrollTrigger: { trigger: el, start: 'top 88%', once: true },
        onUpdate: () => { el.innerHTML = o.v.toFixed(dp).replace('.', '<span class="dot">.</span>'); },
        onComplete: () => { el.innerHTML = finalHTML; },
      });
    });
  });

  // ---------- app: the window lands flat and the mask lands on it ----------
  guard('app', () => {
    const stage = document.querySelector('.app__stage');
    if (!stage) return;
    gsap.timeline({
      defaults: { ease: 'none' },
      scrollTrigger: { trigger: stage, start: 'top 95%', end: 'center 45%', scrub: 0.7 },
    })
      .fromTo('.app__tilt', { rotateX: 34, scale: 0.84, y: 60 }, { rotateX: 0, scale: 1, y: 0, duration: 1 }, 0)
      .fromTo('.app__beams', { z: 320, opacity: 0.2 }, { z: 0, opacity: 1, duration: 1 }, 0)
      .fromTo('.app__on', { opacity: 0 }, { opacity: 1, duration: 0.3 }, 0.66);
  });

  // ---------- speed: scrolling drags the exposure slider through seven renders ----------
  guard('speed', () => {
    const fig = document.querySelector('[data-sweep]');
    const input = fig && fig.querySelector('input[type="range"]');
    if (!input) return;
    const s = { v: 0 };
    const move = () => drive(fig, s.v);
    move();
    const max = Number(input.max);
    mm.add('(min-width: 901px)', () => {
      gsap.to(s, {
        v: max, ease: 'none', onUpdate: move,
        scrollTrigger: { trigger: '.speed__grid', start: `center center+=${NAV / 2}`, end: '+=110%', pin: true, scrub: 0.6 },
      });
    });
    mm.add('(max-width: 900px)', () => {
      gsap.to(s, {
        v: max, ease: 'none', onUpdate: move,
        scrollTrigger: { trigger: fig, start: 'top 80%', end: 'bottom 30%', scrub: 0.6 },
      });
    });
  });

  // ---------- research: the papers stack, and scrolling turns one page at a time ----------
  guard('research', () => {
    const section = document.querySelector('.research');
    const papers = section ? q('.paper', section) : [];
    if (papers.length < 2) return;
    const count = section.querySelector('[data-paper-n]');
    const n = papers.length;
    const STEP = 9, SHRINK = 0.018;   // how far each page below the top one sits
    section.classList.add('is-stacked');
    papers.forEach((p, k) => gsap.set(p, { zIndex: n - k, y: k * STEP, scale: 1 - k * SHRINK, transformPerspective: 1600 }));
    const tl = gsap.timeline({
      defaults: { ease: 'none' },
      // on the timeline, not the trigger: the scrub keeps moving it after the scroll stops
      onUpdate: () => { if (count) count.textContent = String(Math.min(n, Math.floor(tl.time() + 0.5) + 1)); },
      scrollTrigger: {
        trigger: section.querySelector('.research__grid'), start: `center center+=${NAV / 2}`, end: `+=${(n - 1) * 42}%`,
        pin: true, scrub: 0.6,
      },
    });
    // a page lifts from its bottom edge toward you and over the top, and the pile rises
    for (let i = 0; i < n - 1; i++) {
      // the turn stays visible the whole way; the page only fades in the last stretch, near edge-on
      tl.to(papers[i], { rotationX: 90, y: -30, '--shade': 1, duration: 1, ease: 'power1.inOut' }, i)
        .to(papers[i], { autoAlpha: 0, duration: 0.14 }, i + 0.86);
      for (let k = i + 1; k < n; k++) {
        tl.to(papers[k], { y: (k - i - 1) * STEP, scale: 1 - (k - i - 1) * SHRINK, duration: 0.6, ease: 'power2.out' }, i + 0.4);
      }
    }
    // A direct route to every citation, including by keyboard. Focusing a paper
    // under the pile must not leave the focused link covered by another sheet.
    const browse = document.createElement('button');
    browse.type = 'button';
    browse.className = 'btn btn--ghost research__browse';
    browse.textContent = 'View all papers';
    browse.setAttribute('aria-controls', 'papers');
    const expand = () => {
      const top = section.getBoundingClientRect().top + scrollY;
      tl.scrollTrigger.kill(true);
      tl.kill();
      gsap.set(papers, { clearProps: 'all' });
      section.classList.remove('is-stacked');
      browse.remove();
      ScrollTrigger.refresh();
      window.__lenis ? window.__lenis.scrollTo(top - NAV, { immediate: true }) : window.scrollTo(0, top - NAV);
    };
    browse.addEventListener('click', () => {
      expand();
      section.querySelector('.paper a')?.focus({ preventScroll: true });
    });
    section.querySelector('.research__head').append(browse);
    section.querySelector('.papers').addEventListener('focusin', () => {
      if (section.classList.contains('is-stacked')) expand();
    });
  });

  // ---------- assistant: typed, called, and the proposal slides over yours ----------
  guard('assistant', () => {
    const stage = document.querySelector('.assistant__scene');
    if (!stage) return;
    const typed = stage.querySelector('[data-typed]');
    const said = stage.querySelector('.agent__said');
    const prompt = said ? said.textContent : '';
    const steps = q('[data-step]', stage);
    const photo = stage.querySelector('[data-compare]');
    const rest = photo ? Number(photo.querySelector('input[type="range"]').value) : 0;
    const wipe = gsap.parseEase('power2.inOut');
    const render = p => {
      if (typed) typed.textContent = p < 0.3 ? prompt.slice(0, Math.round(prompt.length * Math.min(1, p / 0.26))) : '';
      steps.forEach(el => el.classList.toggle('is-shown', p >= parseFloat(el.dataset.step)));
      // while the calls run the proposal slides across, and it ends almost all the way over yours
      if (photo) drive(photo, 100 - (100 - rest) * wipe(gsap.utils.clamp(0, 1, (p - 0.44) / 0.46)));
    };
    const staged = scrollTrigger => {
      stage.classList.add('is-staged');
      const st = { p: 0 };
      render(0);
      gsap.to(st, { p: 1, ease: 'none', onUpdate: () => render(st.p), scrollTrigger });
      return () => { stage.classList.remove('is-staged'); render(1); };
    };
    // on a wide screen the whole section holds still, heading and all, while the transcript runs
    mm.add('(min-width: 901px)', () => staged({ trigger: stage, start: `center center+=${NAV / 2}`, end: '+=150%', pin: true, scrub: 0.5 }));
    mm.add('(max-width: 900px)', () => staged({ trigger: stage.querySelector('.agent'), start: 'top 72%', end: 'bottom 80%', scrub: 0.5 }));
  });

  // ---------- close: the pier flies in out of Orion's Belt, the sky falls away, and it develops ----------
  guard('close', () => {
    const close = document.querySelector('.alpha');
    const frame = close && close.querySelector('.alpha__frame');
    if (!frame) return;
    const s = { pos: 100 };
    const move = () => drive(frame, s.pos);
    move();
    const copy = q('.alpha__copy .wrap > *', close);
    gsap.set(frame, { transformPerspective: 1400 });
    // Landing, the hero's lift-off played backwards: the photograph comes up the page as the card the
    // hero became (the same size and lean), settles flat to fill the screen, and black takes the sky.
    const flyIn = (tl, span) => tl
      .fromTo(frame, { scale: 0.42, rotationX: 26, yPercent: 6, borderRadius: 28 }, { scale: 1, rotationX: 0, yPercent: 0, borderRadius: 0, duration: span, ease: 'power2.inOut' }, 0)
      .fromTo(sky, { vis: 1, warp: 0 }, { vis: 0, warp: 0.6, duration: span, ease: 'power2.in', immediateRender: false }, 0);
    mm.add('(min-width: 761px)', () => {
      const tl = gsap.timeline({ scrollTrigger: { trigger: close, start: 'top top', end: '+=200%', pin: true, scrub: 0.8 } });
      flyIn(tl, 1)
        .to(s, { pos: 0, duration: 0.8, ease: 'power1.inOut', onUpdate: move }, 1)
        .from(copy, { y: 40, opacity: 0, duration: 0.4, stagger: 0.08, ease: 'power2.out' }, 1.5);
    });
    mm.add('(max-width: 760px)', () => {
      const tl = gsap.timeline({ scrollTrigger: { trigger: close, start: 'top bottom', end: 'top top', scrub: 0.6 } });
      flyIn(tl, 0.7)
        .to(s, { pos: 0, duration: 0.3, ease: 'power1.inOut', onUpdate: move }, 0.7)
        .from(copy, { y: 30, opacity: 0, duration: 0.2, stagger: 0.05, ease: 'power2.out' }, 0.8);
    });
  });

  addEventListener('load', () => ScrollTrigger.refresh());
})();
