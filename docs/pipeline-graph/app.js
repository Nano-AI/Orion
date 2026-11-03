// The page: a filmstrip of stage pictures, a before/after viewer, and the graph.
// Pictures and timings come from stages/manifest.js, written by
//   ./build/apps/bench/orion-bench <raw> --stages docs/pipeline-graph/stages --demo
(function () {
  const G = window.OrionGraph;
  const M = window.ORION_STAGES || { stages: [], timings: {} };
  const $ = s => document.querySelector(s);
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const reduceMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const ms = v => v >= 10 ? v.toFixed(0) : v.toFixed(1);
  const cost = ids => [...ids].reduce((t, id) => t + (M.timings[G.byId[id].label] || 0), 0);

  // ── The frames: one per stage picture, in graph order ──
  // `vs` is the frame this one is compared against; null for side branches,
  // whose output is a map or a channel rather than the picture.
  const FRAMES = [
    { slug: 'source', stage: 'source', title: 'Source mosaic', vs: null,
      body: 'What the sensor recorded: one brightness sample per pixel, each behind a red, green or blue filter. In the detail view the Bayer checkerboard shows.' },
    { slug: 'linearize', stage: 'demosaic', title: 'Linearize', vs: 'source',
      body: 'Subtracts the black level, scales to the white point and applies the white-balance multipliers. Still one sample per pixel.' },
    { slug: 'demosaic', stage: 'demosaic', title: 'Demosaic', vs: 'linearize',
      body: 'RCD interpolation estimates the two missing colors at every pixel, working out edge direction first so it interpolates along edges, not across them. The colors are still the camera’s own.' },
    { slug: 'highlights', stage: 'highlights', title: 'Highlight reconstruction', vs: 'demosaic',
      body: 'Where one channel clipped, it is rebuilt from the channels that did not (Masood, Zhu and Tappen). A pull-push fill then carries color across cores where all three blew out.' },
    { slug: 'denoise', stage: 'denoise', title: 'Wavelet denoise', vs: 'highlights',
      body: 'A four-scale starlet transform shrinks noise in linear camera RGB, the only place the sensor’s noise model holds.' },
    { slug: 'lens', stage: 'lens', title: 'Lens correction', vs: 'denoise',
      body: 'Distortion, chromatic aberration and vignetting. The one capture stage that moves pixels instead of recoloring them.' },
    { slug: 'sharpen', stage: 'sharpen', title: 'Capture sharpening', vs: 'lens',
      body: 'Restores the softness that demosaicing and the lens resample cost. It sits before every tone control, so moving exposure never re-runs it.' },
    { slug: 'matrix', stage: 'profile', title: 'Camera to working space', vs: 'sharpen',
      body: 'A 3 × 3 matrix takes camera RGB into scene-linear Rec.2020, the space every later stage works in.' },
    { slug: 'profile', stage: 'profile', title: 'Camera profile', vs: 'matrix',
      body: 'The DNG hue/saturation map corrects the colors a single matrix cannot get right, before any creative adjustment.' },
    { slug: 'dehaze', stage: 'dehaze', title: 'Dehaze', vs: 'profile',
      body: 'The dark channel prior (He, Sun and Tang) estimates haze from each patch’s darkest channel, refines that map with a guided filter, then removes the haze in linear light.' },
    { slug: 'clarity-luma', stage: 'clarity', title: 'Clarity input', vs: null,
      body: 'The local Laplacian filter works on a single channel, normalized log luminance. This is that channel.' },
    { slug: 'clarity', stage: 'clarity', title: 'Clarity', vs: 'dehaze',
      body: 'A local Laplacian pyramid (Paris et al. 2011, Aubry et al. 2014) raises mid-scale contrast without halos. Six pyramid levels and sixteen remapped copies make 36 nodes.' },
    { slug: 'fusion', stage: 'fusion', title: 'Exposure fusion', vs: 'clarity',
      body: 'Simulates an exposure bracket from one frame and blends the best-exposed parts of each (Hessel and Morel), lifting shadows while keeping local contrast.' },
    { slug: 'guide', stage: 'guide', title: 'Shadows and highlights guide', vs: null,
      body: 'A guided filter at a quarter of the resolution. This is one of its coefficient fields, stretched to fit. The tone stage uses it to lift shadows and pull highlights by region instead of by pixel.' },
    { slug: 'mask', stage: 'masks', title: 'Mask: linear gradient', vs: null,
      body: 'One mask component, a gradient from the top of the frame. White is full coverage. Masks depend only on position, so the shader has no picture input.' },
    { slug: 'mask-refine', stage: 'refine', title: 'Mask edge refine', vs: 'mask',
      body: 'Guided feathering pulls the mask’s edge toward edges in the photograph.' },
    { slug: 'tone', stage: 'tone', title: 'Tone and color', vs: 'fusion',
      body: 'One shader applies every scene-linear adjustment at once: exposure, whites, blacks, highlights, shadows, vibrance, and the mask’s −1 EV on the sky.' },
    { slug: 'grade', stage: 'grade', title: 'Grade and vignette', vs: 'tone',
      body: 'Three-way color grading, one ASC CDL per tonal zone (cool shadows and warm highlights here), plus the creative vignette.' },
    { slug: 'display', stage: 'display', title: 'Display transform', vs: 'grade',
      body: 'Contrast, the tone curve and any creative LUT, then the transform from scene-linear light to display values.' },
    { slug: 'grain', stage: 'grain', title: 'Grain and dither', vs: 'display',
      body: 'Film grain is added to unquantized values, then the picture is dithered and quantized to 8 bits.' },
    { slug: 'geometry', stage: 'geometry', title: 'Crop and rotate', vs: 'source',
      body: 'A 2° straighten, then the crop. The only node that changes the picture’s size. This is the picture Orion shows on screen. On the left is the sensor data it started from.' },
  ];
  const shot = Object.fromEntries(M.stages.map(s => [s.file.replace(/\.jpg$/, ''), s]));
  const frames = FRAMES.filter(f => shot[f.slug]);
  const frameBySlug = Object.fromEntries(frames.map(f => [f.slug, f]));
  // Zoom on the subject the bench found (the brightest region), so a small
  // subject in a big frame is still the thing you look at.
  const SUBJECT_ZOOM = 2.6;
  const subject = M.subject;
  if (subject) {
    const root = document.documentElement.style;
    root.setProperty('--sx', (subject[0] * 100).toFixed(1) + '%');
    root.setProperty('--sy', (subject[1] * 100).toFixed(1) + '%');
    root.setProperty('--tz', String(SUBJECT_ZOOM));
  }
  let zoomed = !!subject;

  // ── Header facts ──
  const kernels = G.N.filter(n => !n.isAux);
  const mp = M.fullSize ? (M.fullSize[0] * M.fullSize[1] / 1e6).toFixed(0) : null;
  $('#facts').innerHTML =
    `<div><b>${kernels.length}</b> shaders</div><div><b>${frames.length}</b> stages shown</div>` +
    (mp ? `<div><b>${mp} MP</b> ${esc(M.camera)}</div>` : '');

  // A model that has fallen behind the engine says so, instead of quietly lying.
  const missing = Object.keys(M.timings).filter(name => !(name in G.byLabel));
  if (missing.length) {
    const w = $('#drift');
    w.hidden = false;
    w.textContent = `The engine reports ${missing.length} node${missing.length > 1 ? 's' : ''} this page does not model (${missing.slice(0, 4).join(', ')}${missing.length > 4 ? ', …' : ''}). Update graph.js to match.`;
  }
  if (!M.stages.length) {
    $('#viewer').innerHTML = '<p class="empty">No stage pictures yet. Run <code>orion-bench &lt;raw&gt; --stages docs/pipeline-graph/stages --demo</code> from the repository root, then reload.</p>';
  }

  // ── Filmstrip ──
  const strip = $('#strip');
  frames.forEach((f, i) => {
    const b = document.createElement('button');
    b.className = 'frame';
    b.type = 'button';
    b.dataset.slug = f.slug;
    b.setAttribute('aria-pressed', 'false');
    b.setAttribute('tabindex', i === 0 ? '0' : '-1');
    b.innerHTML = `<span class="thumb"><img src="stages/${f.slug}.jpg" alt="" loading="lazy" width="160" height="107"></span><span class="fnum">${i + 1}</span><span class="fname">${esc(f.title)}</span>`;
    b.addEventListener('click', () => showFrame(f.slug, true));
    strip.appendChild(b);
  });
  strip.addEventListener('keydown', e => {
    const keys = { ArrowRight: 1, ArrowLeft: -1, Home: -Infinity, End: Infinity };
    if (!(e.key in keys)) return;
    e.preventDefault();
    const btns = [...strip.querySelectorAll('.frame')];
    const i = btns.indexOf(document.activeElement);
    const j = Math.max(0, Math.min(btns.length - 1, (i < 0 ? 0 : i) + keys[e.key]));
    btns[j].focus();
    showFrame(btns[j].dataset.slug, true);
  });

  // ── Viewer ──
  let frame = null, detail = false;
  const cmp = $('#cmp');
  function setSplit(v) { $('#viewer').style.setProperty('--split', v + '%'); }
  cmp.addEventListener('input', () => setSplit(cmp.value));

  function showFrame(slug, fromUser) {
    const f = frameBySlug[slug];
    if (!f) return;
    frame = f;
    if (fromUser) history.replaceState(null, '', '#' + f.slug);
    const s = shot[f.slug];
    const vs = f.vs && frameBySlug[f.vs] ? f.vs : null;
    const hasCrop = !!s.crop;
    if (!hasCrop) detail = false;
    const vsHasCrop = vs && shot[vs].crop;
    const comparing = !!vs && (!detail || vsHasCrop);
    const file = sl => `stages/${detail && shot[sl].crop ? sl + '-crop' : sl}.jpg`;

    strip.querySelectorAll('.frame').forEach(b => {
      const on = b.dataset.slug === slug;
      b.setAttribute('aria-pressed', on);
      b.setAttribute('tabindex', on ? '0' : '-1');
      if (on && fromUser) b.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: reduceMotion ? 'auto' : 'smooth' });
    });

    const after = $('#after'), before = $('#before');
    after.src = file(f.slug);
    after.alt = `${f.title}: the output of ${s.node}${detail ? ', 1:1 detail' : ''}`;
    before.hidden = !comparing;
    cmp.hidden = !comparing;
    $('#viewer').classList.toggle('comparing', comparing);
    $('#viewer').classList.toggle('pixel', detail);
    // Zoomed, the subject is scaled and moved to the centre so the divider runs through it.
    $('#viewer').style.setProperty('--z', zoomed && !detail ? SUBJECT_ZOOM : 1);
    $('#viewer').style.setProperty('--c', zoomed && !detail ? 1 : 0);
    if (comparing) {
      before.src = file(vs);
      before.alt = `${frameBySlug[vs].title}, the previous stage`;
      setSplit(cmp.value);
    }
    $('#lblBefore').textContent = comparing ? frameBySlug[vs].title : '';
    $('#lblAfter').textContent = f.title;
    $('#lblBefore').hidden = !comparing;

    const ids = G.N.filter(n => n.stage === f.stage && !n.isAux).map(n => n.id);
    const stageMs = cost(ids);
    const idx = frames.indexOf(f);
    $('#cap').innerHTML = `
      <p class="step">Stage ${idx + 1} of ${frames.length}, ${esc(G.STAGES[f.stage].label.toLowerCase())}</p>
      <h2 id="capTitle">${esc(f.title)}</h2>
      <p class="body">${esc(f.body)}</p>
      <dl class="facts">
        <dt>Shown node</dt><dd><code>${esc(s.node)}</code></dd>
        <dt>Format</dt><dd>${esc(s.format)}, ${s.size[0]} × ${s.size[1]}</dd>
        <dt>Stage</dt><dd>${ids.length} shader${ids.length === 1 ? '' : 's'}${stageMs ? `, ${ms(stageMs)} ms at ${mp} MP` : ''}</dd>
      </dl>
      <div class="capActions">
        ${subject && !detail ? `<button type="button" id="zoomBtn" class="ghost" aria-pressed="${zoomed}">${zoomed ? 'Show whole frame' : 'Zoom to subject'}</button>` : ''}
        ${hasCrop ? `<button type="button" id="detailBtn" class="ghost" aria-pressed="${detail}">${detail ? 'Show whole frame' : 'Show 1:1 detail'}</button>` : ''}
        <button type="button" id="toGraph" class="ghost">Find in graph</button>
      </div>
      <nav class="stepper" aria-label="Stages">
        <button type="button" id="prevF" class="ghost" ${idx === 0 ? 'disabled' : ''}>Previous stage</button>
        <button type="button" id="nextF" class="solid" ${idx === frames.length - 1 ? 'disabled' : ''}>Next stage</button>
      </nav>`;
    $('#zoomBtn')?.addEventListener('click', () => { zoomed = !zoomed; showFrame(f.slug); $('#zoomBtn').focus(); });
    $('#detailBtn')?.addEventListener('click', () => { detail = !detail; showFrame(f.slug); $('#detailBtn').focus(); });
    $('#toGraph').addEventListener('click', () => {
      selectGraph({ type: 'stage', key: f.stage }, true);
      $('#graph').scrollIntoView({ behavior: reduceMotion ? 'auto' : 'smooth' });
    });
    $('#prevF').addEventListener('click', () => showFrame(frames[idx - 1].slug, true));
    $('#nextF').addEventListener('click', () => showFrame(frames[idx + 1].slug, true));
    if (fromUser && cy) selectGraph({ type: 'stage', key: f.stage }, false);
  }

  // ── Graph ──
  const css = v => getComputedStyle(document.documentElement).getPropertyValue(v).trim();
  const regionColor = r => css(G.REGIONS[r].color);
  let cy = null, view = 'stages', current = null;

  function nodeElements() {
    const els = Object.entries(G.STAGES).map(([k, s]) => ({ data: { id: 'g_' + k, label: s.label, region: s.region, kind: 'group' } }));
    for (const n of G.N) els.push({ data: { id: n.id, label: n.label, parent: 'g_' + n.stage, region: G.STAGES[n.stage].region, kind: n.isAux ? 'aux' : 'node' } });
    for (const n of G.N) {
      for (const s of new Set(n.inputs)) els.push({ data: { id: s + '>' + n.id, source: s, target: n.id, aux: false } });
      for (const s of new Set(n.aux)) els.push({ data: { id: s + '>' + n.id, source: s, target: n.id, aux: true } });
    }
    return els;
  }
  function stageElements() {
    const els = Object.entries(G.STAGES).map(([k, s]) => {
      const c = G.N.filter(n => n.stage === k && !n.isAux).length;
      return { data: { id: 'g_' + k, label: s.label + (c ? `\n${c} shader${c > 1 ? 's' : ''}` : ''), region: s.region, kind: 'stage' } };
    });
    const seen = new Set();
    for (const n of G.N) for (const s of [...n.inputs, ...n.aux]) {
      const a = G.byId[s].stage, b = n.stage;
      if (a !== b && !seen.has(a + b)) { seen.add(a + b); els.push({ data: { id: 'e_' + a + '_' + b, source: 'g_' + a, target: 'g_' + b } }); }
    }
    return els;
  }
  function style() {
    const text = css('--text'), dim = css('--dim'), line = css('--edge'), raised = css('--raised');
    const dirty = css('--dirty'), deps = css('--deps'), accent = css('--accent'), ground = css('--ground');
    return [
      { selector: 'node', style: { label: 'data(label)', 'font-family': 'Martian Mono, ui-monospace, monospace', 'font-size': 9, color: text,
        'text-valign': 'center', 'text-halign': 'center', 'background-color': raised, 'border-width': 1.5,
        'border-color': e => regionColor(e.data('region')), shape: 'round-rectangle', width: 'label', height: 22, padding: '7px', 'text-wrap': 'wrap' } },
      { selector: 'node[kind="aux"]', style: { shape: 'cut-rectangle', 'border-style': 'dashed', color: dim } },
      { selector: 'node[kind="stage"]', style: { 'font-family': 'Archivo, system-ui, sans-serif', 'font-size': 13, 'font-weight': 600,
        height: 40, padding: '10px', 'text-max-width': 200, 'border-width': 2 } },
      { selector: ':parent', style: { 'text-valign': 'top', 'text-halign': 'center', 'font-family': 'Archivo, system-ui, sans-serif',
        'font-size': 14, 'font-weight': 600, color: e => regionColor(e.data('region')), 'background-color': e => regionColor(e.data('region')),
        'background-opacity': 0.05, 'border-width': 1, 'border-opacity': 0.45, padding: '16px', shape: 'round-rectangle' } },
      { selector: 'edge', style: { width: 1.2, 'line-color': line, 'target-arrow-color': line, 'target-arrow-shape': 'triangle', 'arrow-scale': 0.7, 'curve-style': 'bezier' } },
      { selector: 'edge[?aux]', style: { 'line-style': 'dashed' } },
      { selector: '.dim', style: { opacity: 0.16 } },
      { selector: 'node.up', style: { 'border-color': deps, 'border-width': 3 } },
      { selector: 'node.down', style: { 'border-color': dirty, 'border-width': 3, 'background-color': dirty, 'background-opacity': 0.16 } },
      { selector: 'node[kind="stage"].down', style: { 'background-opacity': 0.3 } },
      { selector: 'edge.up', style: { 'line-color': deps, 'target-arrow-color': deps, width: 2 } },
      { selector: 'edge.down', style: { 'line-color': dirty, 'target-arrow-color': dirty, width: 2 } },
      { selector: 'node.sel', style: { 'border-color': accent, 'border-width': 3, 'background-color': accent, 'background-opacity': 1, color: ground } },
      { selector: ':parent.sel', style: { 'background-opacity': 0.12, color: accent } },
    ];
  }
  // Stages read top to bottom so the chain fits the canvas; every shader reads left to right.
  const layout = () => ({ name: 'dagre', rankDir: view === 'stages' ? 'TB' : 'LR', nodeSep: view === 'stages' ? 24 : 8, rankSep: view === 'stages' ? 22 : 40, edgeSep: 4, animate: false, fit: true, padding: 28 });

  function build() {
    if (cy) cy.destroy();
    cy = cytoscape({ container: $('#cy'), elements: view === 'stages' ? stageElements() : nodeElements(),
      style: style(), wheelSensitivity: 0.25, minZoom: 0.05, maxZoom: 3, boxSelectionEnabled: false, autoungrabify: true });
    cy.layout(layout()).run();
    cy.on('tap', 'node', e => {
      const d = e.target.data();
      const sel = d.kind === 'group' || d.kind === 'stage' ? { type: 'stage', key: d.id.slice(2) } : { type: 'node', key: d.id };
      selectGraph(sel, false);
      const st = sel.type === 'stage' ? sel.key : G.byId[sel.key].stage;
      const f = frames.find(fr => fr.stage === st);
      if (f && f !== frame) showFrame(f.slug, false);
    });
    cy.on('dbltap', 'node[kind="stage"]', e => { const k = e.target.id().slice(2); setView('nodes'); selectGraph({ type: 'stage', key: k }, true); });
    cy.on('tap', e => { if (e.target === cy) selectGraph(null); });
    highlight();
  }
  function setView(v) {
    if (v === view) return;
    view = v;
    $('#vStages').setAttribute('aria-pressed', v === 'stages');
    $('#vNodes').setAttribute('aria-pressed', v === 'nodes');
    build();
  }
  function sets(sel) {
    const touched = new Set(sel.type === 'node' ? [sel.key]
      : sel.type === 'stage' ? G.N.filter(n => n.stage === sel.key).map(n => n.id)
      : G.SLIDERS[sel.key].ids);
    const down = new Set([...G.downstream([...touched])].filter(x => !touched.has(x)));
    const up = sel.type === 'slider' ? new Set() : new Set([...G.upstream([...touched])].filter(x => !touched.has(x)));
    return { touched, down, up };
  }
  function highlight() {
    if (!cy) return;
    cy.elements().removeClass('dim up down sel');
    if (!current) return;
    const { touched, down, up } = sets(current);
    cy.elements().addClass('dim');
    if (view === 'nodes') {
      const mark = (ids, cls) => ids.forEach(id => { const el = cy.getElementById(id); el.removeClass('dim').addClass(cls); el.parent().removeClass('dim'); });
      mark(touched, 'sel'); mark(down, 'down'); mark(up, 'up');
      cy.edges().forEach(e => {
        const s = e.source().id(), t = e.target().id();
        if ((touched.has(s) || down.has(s)) && down.has(t)) e.removeClass('dim').addClass('down');
        else if (up.has(s) && (up.has(t) || touched.has(t))) e.removeClass('dim').addClass('up');
      });
      if (current.type === 'stage') cy.getElementById('g_' + current.key).removeClass('dim').addClass('sel');
    } else {
      const st = ids => new Set([...ids].map(id => G.byId[id].stage));
      const sT = st(touched), sD = st(down), sU = st(up);
      for (const k of Object.keys(G.STAGES)) {
        const el = cy.getElementById('g_' + k);
        if (sT.has(k)) el.removeClass('dim').addClass('sel');
        else if (sD.has(k)) el.removeClass('dim').addClass('down');
        else if (sU.has(k)) el.removeClass('dim').addClass('up');
      }
      cy.edges().forEach(e => {
        const s = e.source().id().slice(2), t = e.target().id().slice(2);
        if ((sT.has(s) || sD.has(s)) && sD.has(t) && !sT.has(t)) e.removeClass('dim').addClass('down');
        else if (sU.has(s) && (sU.has(t) || sT.has(t)) && !sD.has(t)) e.removeClass('dim').addClass('up');
      });
    }
  }
  function selectGraph(sel, focus) {
    current = sel;
    document.querySelectorAll('#sliders button').forEach((b, i) => b.setAttribute('aria-pressed', !!sel && sel.type === 'slider' && sel.key === i));
    highlight(); inspect();
    if (focus && sel && cy) {
      const target = sel.type === 'stage' ? cy.getElementById('g_' + sel.key) : cy.getElementById(sel.key);
      // A node is shown with its direct neighbours, never blown up on its own.
      const eles = sel.type === 'node' ? target.closedNeighborhood() : target;
      const clamp = () => { if (cy.zoom() > 1.4) { cy.zoom(1.4); cy.center(eles); } };
      if (target.nonempty()) cy.animate({ fit: { eles, padding: 60 }, duration: reduceMotion ? 0 : 300, complete: clamp });
    }
  }

  // ── Inspector ──
  const link = id => `<a href="#graph" data-id="${id}">${esc(G.byId[id].label)}</a>`;
  function grouped(ids, max = 14) {
    const g = {};
    for (const id of ids) (g[G.byId[id].stage] ??= []).push(id);
    return Object.keys(G.STAGES).filter(k => g[k]).map(k => {
      const list = g[k], shown = list.slice(0, max);
      return `<div class="grp"><div class="grpHead"><span>${esc(G.STAGES[k].label)}</span><span>${list.length}</span></div><div>${shown.map(link).join(' ')}${list.length > max ? ` <span class="more">and ${list.length - max} more</span>` : ''}</div></div>`;
    }).join('');
  }
  function inspect() {
    const el = $('#insp');
    if (!current) {
      const keep = kernels.length - G.downstream([G.byLabel['develop:linear']]).size - 1;
      el.innerHTML = `<h3>Why the graph is a graph</h3>
        <p>Each node is one compute shader and a pure function of its inputs. Moving a slider writes new parameters into the nodes it owns, and those nodes and everything downstream are marked dirty. The next frame re-runs only the dirty set.</p>
        <p>So placement is a performance decision. Dehaze, clarity, fusion and the guide sit before exposure, which keeps ${keep} shaders cached while you drag the slider people use most.</p>
        <p class="note">Pick a control on the left, or click any box. A disabled node passes its first input through, which is why input order matters.</p>`;
      return;
    }
    const { touched, down, up } = sets(current);
    let head = '', body = '';
    if (current.type === 'node') {
      const n = G.byId[current.key], st = G.STAGES[n.stage];
      const t = M.timings[n.label];
      head = `<p class="chipRow"><span class="chip" style="--c:var(${G.REGIONS[st.region].color})">${esc(st.label)}</span></p><h3 class="mono">${esc(n.label)}</h3>`;
      body = `<dl class="facts"><dt>Shader</dt><dd><code>${esc(n.kernel)}</code></dd><dt>Output</dt><dd>${esc(n.fmt)}, ${esc(n.size)}</dd>${t ? `<dt>GPU time</dt><dd>${ms(t)} ms at ${mp} MP</dd>` : ''}<dt>Built in</dt><dd>${esc(st.file)}</dd></dl>`;
      if (G.NOTES[n.label]) body += `<p class="note">${esc(G.NOTES[n.label])}</p>`;
      if (n.inputs.length) body += `<div><h4>Inputs, in order</h4><ol class="ports">${n.inputs.map(i => `<li>${link(i)}</li>`).join('')}</ol></div>`;
      if (n.aux.length) body += `<div><h4>Uploaded textures it reads</h4><p>${n.aux.map(link).join(' ')}</p></div>`;
      const ctl = G.SLIDERS.filter(s => s.ids.includes(n.id)).map(s => s.name);
      if (ctl.length) body += `<div><h4>Controlled by</h4><p>${ctl.map(esc).join(', ')}</p></div>`;
    } else if (current.type === 'stage') {
      const st = G.STAGES[current.key];
      head = `<p class="chipRow"><span class="chip" style="--c:var(${G.REGIONS[st.region].color})">${G.REGIONS[st.region].label}</span></p><h3>${esc(st.label)}</h3>`;
      body = `<p class="note">Shown as if every node in this stage changed. Built in ${esc(st.file)}.</p><div class="list">${grouped([...touched], 40)}</div>`;
      if (view === 'stages') body += `<p><button type="button" class="ghost" id="openStage">Show this stage’s shaders</button></p>`;
    } else {
      const s = G.SLIDERS[current.key];
      head = `<p class="chipRow"><span class="chip" style="--c:var(--accent)">Control</span></p><h3>${esc(s.name)}</h3>`;
      body = `<dl class="facts"><dt>Pushed by</dt><dd><code>${esc(s.fn)}()</code></dd></dl>`;
      if (s.note) body += `<p class="note">${esc(s.note)}</p>`;
      body += `<div><h4>Writes parameters into</h4><div class="list">${grouped([...touched])}</div></div>`;
    }
    const rerun = new Set([...touched, ...down].filter(id => !G.byId[id].isAux));
    const rerunMs = cost(rerun);
    body += `<div class="impact">
      <div class="meter" role="img" aria-label="${rerun.size} of ${kernels.length} shaders re-run"><span style="width:${100 * rerun.size / kernels.length}%"></span></div>
      <p><b>${rerun.size}</b> of ${kernels.length} shaders re-run${rerunMs ? `, about <b>${ms(rerunMs)} ms</b> of GPU work` : ''}. ${kernels.length - rerun.size} stay cached.</p></div>`;
    if (down.size) body += `<div><h4 class="dirtyH">Goes dirty when this changes</h4><div class="list">${grouped([...down])}</div></div>`;
    else body += `<p class="note">Nothing reads this node. It is the end of the graph.</p>`;
    if (up.size) body += `<div><h4 class="depsH">Depends on</h4><div class="list">${grouped([...up], 8)}</div></div>`;
    el.innerHTML = head + body;
  }
  $('#insp').addEventListener('click', e => {
    const a = e.target.closest('a[data-id]');
    if (a) { e.preventDefault(); setView('nodes'); selectGraph({ type: 'node', key: a.dataset.id }, true); }
    if (e.target.id === 'openStage') { const k = current.key; setView('nodes'); selectGraph({ type: 'stage', key: k }, true); }
  });

  // ── Controls rail ──
  G.SLIDERS.forEach((s, i) => {
    const b = document.createElement('button');
    b.type = 'button';
    b.setAttribute('aria-pressed', 'false');
    const ids = new Set([...s.ids, ...G.downstream(s.ids)].filter(id => !G.byId[id].isAux));
    const t = cost(ids);
    b.innerHTML = `<span>${esc(s.name)}</span><span class="n" aria-label="${ids.size} shaders re-run${t ? `, ${ms(t)} milliseconds` : ''}">${t ? ms(t) + ' ms' : ids.size}</span>`;
    b.addEventListener('click', () => selectGraph(current && current.type === 'slider' && current.key === i ? null : { type: 'slider', key: i }));
    $('#sliders').appendChild(b);
  });
  $('#names').innerHTML = G.N.map(n => `<option value="${esc(n.label)}"></option>`).join('');
  $('#q').addEventListener('change', e => {
    const v = e.target.value.trim().toLowerCase();
    if (!v) return;
    const hit = G.N.find(n => n.label.toLowerCase() === v) || G.N.find(n => n.label.toLowerCase().includes(v));
    $('#qMsg').textContent = hit ? '' : `No node matches “${e.target.value}”.`;
    if (hit) { setView('nodes'); selectGraph({ type: 'node', key: hit.id }, true); }
  });
  $('#vStages').addEventListener('click', () => setView('stages'));
  $('#vNodes').addEventListener('click', () => setView('nodes'));

  inspect();
  // index.html#denoise opens on that stage, so a stage can be linked to or presented from.
  const fromHash = () => frameBySlug[location.hash.slice(1)];
  if (frames.length) showFrame((fromHash() || frames[frames.length - 1]).slug, false);
  addEventListener('hashchange', () => { const f = fromHash(); if (f && f !== frame) showFrame(f.slug, true); });
  if (window.cytoscape && window.cytoscapeDagre) { cytoscape.use(cytoscapeDagre); build(); $('#cyLoading').hidden = true; }
  else $('#cyLoading').textContent = 'The graph library did not load. Check that the vendor folder sits next to this page.';
})();
