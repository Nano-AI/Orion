// The develop graph, rebuilt by replaying DevelopPipeline's pipeline_.add() calls.
// Sources: engine/src/pipe/DevelopCapture.cpp, DevelopLocal.cpp, DevelopMask.cpp,
// DevelopOutput.cpp. If the engine changes, change this to match: app.js warns on
// the page when the profiled engine reports a node this model does not have.
(function () {
  const REGIONS = {
    source:  { label: 'Source',  color: '--r-source'  },
    capture: { label: 'Capture', color: '--r-capture' },
    local:   { label: 'Local',   color: '--r-local'   },
    mask:    { label: 'Masks',   color: '--r-mask'    },
    output:  { label: 'Output',  color: '--r-output'  },
  };
  const STAGES = {
    source:     { label: 'Source mosaic',            region: 'source',  file: 'Pipeline.cpp' },
    demosaic:   { label: 'Linearize and demosaic',   region: 'capture', file: 'DevelopCapture.cpp' },
    highlights: { label: 'Highlight reconstruction', region: 'capture', file: 'DevelopCapture.cpp' },
    denoise:    { label: 'Wavelet denoise',          region: 'capture', file: 'DevelopCapture.cpp' },
    lens:       { label: 'Lens correction',          region: 'capture', file: 'DevelopCapture.cpp' },
    spots:      { label: 'Spot removal',             region: 'capture', file: 'DevelopCapture.cpp' },
    sharpen:    { label: 'Capture sharpening',       region: 'capture', file: 'DevelopCapture.cpp' },
    profile:    { label: 'Camera profile',           region: 'capture', file: 'DevelopCapture.cpp' },
    dehaze:     { label: 'Dehaze',                   region: 'local',   file: 'DevelopLocal.cpp' },
    clarity:    { label: 'Clarity',                  region: 'local',   file: 'DevelopLocal.cpp' },
    fusion:     { label: 'Exposure fusion',          region: 'local',   file: 'DevelopLocal.cpp' },
    guide:      { label: 'Shadows and highlights guide', region: 'local', file: 'DevelopLocal.cpp' },
    masks:      { label: 'Mask components',          region: 'mask',    file: 'DevelopMask.cpp' },
    refine:     { label: 'Mask edge refine',         region: 'mask',    file: 'DevelopMask.cpp' },
    tone:       { label: 'Tone and color',           region: 'output',  file: 'DevelopOutput.cpp' },
    grade:      { label: 'Grade and vignette',       region: 'output',  file: 'DevelopOutput.cpp' },
    display:    { label: 'Display transform',        region: 'output',  file: 'DevelopOutput.cpp' },
    grain:      { label: 'Grain and dither',         region: 'output',  file: 'DevelopOutput.cpp' },
    geometry:   { label: 'Crop and rotate',          region: 'output',  file: 'DevelopOutput.cpp' },
  };

  const N = [];
  const byLabel = {};
  function add(label, kernel, inputs, fmt, stage, size = 'full res', aux = []) {
    const id = 'n' + N.length;
    N.push({ id, label, kernel, fmt, size, stage, inputs, aux, isAux: false });
    byLabel[label] = id; return id;
  }
  function auxTex(label, fmt, stage, size, kernel = 'uploaded by the CPU, no shader') {
    const id = 'a' + N.length;
    N.push({ id, label, kernel, fmt, size, stage, inputs: [], aux: [], isAux: true });
    byLabel[label] = id; return id;
  }

  // ── Capture: the mosaic head ──
  const src   = auxTex('source mosaic', 'R16Uint', 'source', 'full res, one sample per pixel', 'the decoded RAW, uploaded by setSource()');
  const lin   = add('linearize', 'linearize', [src], 'R32Float', 'demosaic');
  const dirs  = add('rcd:dirs', 'rcdDirs', [lin], 'R32Float', 'demosaic');
  const lpf   = add('rcd:lowpass', 'rcdLpf', [lin], 'R32Float', 'demosaic');
  const green = add('rcd:green', 'rcdGreen', [lin, dirs, lpf], 'R32Float', 'demosaic');
  const rgb   = add('rcd:red/blue', 'rcdRedBlue', [lin, green, dirs], 'RGBA16Float', 'demosaic');

  const hl = add('highlights', 'highlightRecover', [rgb], 'RGBA16Float', 'highlights');
  const HL = 12; // hlfill::levelsFor() at this frame size; it depends on the camera
  const pull = [], push = [];
  pull[0] = add('hl:mask', 'hlMask', [hl, rgb], 'RGBA16Float', 'highlights', '1/4');
  for (let l = 1; l < HL; l++) pull[l] = add('hl:pull ' + l, 'hlPull', [pull[l - 1]], 'RGBA16Float', 'highlights', '1/' + (4 << l));
  push[HL - 1] = pull[HL - 1];
  for (let l = HL - 2; l >= 0; l--) push[l] = add('hl:push ' + l, 'hlPush', [pull[l], push[l + 1]], 'RGBA16Float', 'highlights', '1/' + (4 << l));
  const fill = add('hl:fill', 'hlApply', [hl, push[0], rgb], 'RGBA16Float', 'highlights');

  const DS = 4, blur = [], shrink = [];
  // Fine to coarse, interleaved (#406): each shrink adds its scale to the sum so far.
  for (let j = 0; j < DS; j++) {
    const fine = j === 0 ? fill : blur[j - 1];
    blur[j] = add('denoise:blur ' + j, 'atrousBlur', [fine], 'RGBA16Float', 'denoise');
    shrink[j] = add('denoise:shrink ' + j, 'atrousShrink', [fine, blur[j], j === 0 ? blur[j] : shrink[j - 1]], 'RGBA16Float', 'denoise');
  }
  const lens = add('lens', 'lensCorrect', [shrink[DS - 1]], 'RGBA16Float', 'lens');

  // ── Capture tail ──
  const sm  = add('spots:measure', 'spotMeasure', [lens], 'RGBA32Float', 'spots', '256 × 1, one texel per spot');
  const sa  = add('spots:apply', 'spotApply', [lens, sm], 'RGBA16Float', 'spots');
  const sh  = add('sharpen', 'sharpen', [sa], 'RGBA16Float', 'sharpen');
  const mat = add('camera->working', 'cameraToWorking', [sh], 'RGBA16Float', 'profile');
  const auxHS = auxTex('hue/sat table', 'RGBA32Float', 'profile', 'DNG HueSatMap grid');
  const hs  = add('profile:hue/sat', 'hueSatMap', [mat], 'RGBA16Float', 'profile', 'full res', [auxHS]);

  // ── Local: dehaze ──
  const dChan = add('dehaze:channel min', 'dehazeChannelMin', [hs], 'R16Float', 'dehaze');
  const dH = add('dehaze:dark h', 'dehazeRank', [dChan], 'R16Float', 'dehaze');
  const dV = add('dehaze:dark v', 'dehazeRank', [dH], 'R16Float', 'dehaze');
  add('dehaze:candidates', 'dehazePeak', [dV, hs], 'RGBA16Float', 'dehaze', '1/4');
  const dChanA = add('dehaze:channel min/A', 'dehazeChannelMin', [hs], 'R16Float', 'dehaze');
  const mnH = add('dehaze:min h', 'dehazeRank', [dChanA], 'R16Float', 'dehaze');
  const mnV = add('dehaze:min v', 'dehazeRank', [mnH], 'R16Float', 'dehaze');
  const mxH = add('dehaze:max h', 'dehazeRank', [mnV], 'R16Float', 'dehaze');
  const mxV = add('dehaze:max v', 'dehazeRank', [mxH], 'R16Float', 'dehaze');
  const hp  = add('dehaze:moments', 'dehazePrep', [hs, mxV], 'RGBA32Float', 'dehaze', '1/4');
  const hb1 = add('dehaze:blur h', 'boxBlur4', [hp], 'RGBA32Float', 'dehaze', '1/4');
  const hb2 = add('dehaze:blur v', 'boxBlur4', [hb1], 'RGBA32Float', 'dehaze', '1/4');
  const hab = add('dehaze:coeffs', 'dehazeAb', [hb2], 'RG32Float', 'dehaze', '1/4');
  const hb3 = add('dehaze:blur h2', 'boxBlur', [hab], 'RG32Float', 'dehaze', '1/4');
  const hb4 = add('dehaze:blur v2', 'boxBlur', [hb3], 'RG32Float', 'dehaze', '1/4');
  const dehaze = add('dehaze', 'dehazeRecover', [hs, hb4], 'RGBA16Float', 'dehaze');

  // ── Local: clarity (local Laplacian), 6 levels × 4 packed stacks ──
  const LL = 6, LS = 4, gauss = [], pack = [[]], remapH = [], llOut = [];
  const luma = add('clarity:luma', 'llfLuma', [dehaze], 'R16Float', 'clarity');
  gauss[0] = luma;
  for (let l = 1; l < LL; l++) gauss[l] = add('clarity:gauss ' + l, 'llfDown', [gauss[l - 1]], 'R16Float', 'clarity', '1/' + (1 << l));
  for (let l = 1; l < LL; l++) {
    pack[l] = [];
    for (let s = 0; s < LS; s++) {
      const tag = 'clarity:remap ' + l + '.' + s;
      if (l === 1) {
        remapH[s] = add(tag + ' h', 'llfRemapH', [luma], 'RGBA16Float', 'clarity', '1/2 wide, full height');
        pack[l][s] = add(tag + ' v', 'llfDownV', [remapH[s]], 'RGBA16Float', 'clarity', '1/2');
      } else {
        pack[l][s] = add(tag, 'llfDownPacked', [pack[l - 1][s]], 'RGBA16Float', 'clarity', '1/' + (1 << l));
      }
    }
  }
  llOut[LL - 1] = gauss[LL - 1];
  for (let l = LL - 2; l >= 1; l--)
    llOut[l] = add('clarity:collapse ' + l, 'llfCollapse', [gauss[l], ...pack[l], ...pack[l + 1], llOut[l + 1]], 'R16Float', 'clarity', '1/' + (1 << l));
  llOut[0] = add('clarity:collapse 0', 'llfCollapse0', [luma, ...pack[1], llOut[1]], 'R16Float', 'clarity');
  const clarity = add('clarity', 'llfApply', [dehaze, llOut[0], luma], 'RGBA16Float', 'clarity');

  // ── Local: exposure fusion, 6 levels × 2 stacks, at 1/4 ──
  const FL = 6, FS = 2, fImg = [], fWt = [], fOut = [];
  const proxy = add('fusion:proxy', 'fuseProxy', [clarity], 'R16Float', 'fusion', '1/4');
  for (let l = 0; l < FL; l++) {
    fImg[l] = []; fWt[l] = [];
    const sz = '1/' + (4 << l);
    for (let st = 0; st < FS; st++) {
      const tag = '.' + l + '.' + st;
      fImg[l][st] = l === 0 ? add('fusion:images' + tag, 'fuseSplit', [proxy], 'RGBA16Float', 'fusion', sz)
                            : add('fusion:images' + tag, 'llfDownPacked', [fImg[l - 1][st]], 'RGBA16Float', 'fusion', sz);
      fWt[l][st]  = l === 0 ? add('fusion:weights' + tag, 'fuseSplit', [proxy], 'RGBA16Float', 'fusion', sz)
                            : add('fusion:weights' + tag, 'llfDownPacked', [fWt[l - 1][st]], 'RGBA16Float', 'fusion', sz);
    }
  }
  for (let l = FL - 1; l >= 0; l--) {
    const res = l === FL - 1, c = res ? l : l + 1;
    fOut[l] = add('fusion:blend ' + l, 'fuseBlend', [fImg[l][0], fImg[l][1], fWt[l][0], fWt[l][1], fImg[c][0], fImg[c][1], res ? fWt[l][0] : fOut[l + 1]], 'R16Float', 'fusion', '1/' + (4 << l));
  }
  const fusion = add('fusion', 'fuseApply', [clarity, fOut[0], proxy], 'RGBA16Float', 'fusion');

  // ── Local: guided filter for shadows and highlights ──
  const gp  = add('guide:prep', 'guidePrep', [hs], 'RG32Float', 'guide');
  const gd  = add('guide:subsample', 'guideDown', [gp], 'RG32Float', 'guide', '1/4');
  const g1  = add('guide:blur h', 'boxBlur', [gd], 'RG32Float', 'guide', '1/4');
  const g2  = add('guide:blur v', 'boxBlur', [g1], 'RG32Float', 'guide', '1/4');
  const gab = add('guide:coeffs', 'guideAb', [g2], 'RG32Float', 'guide', '1/4');
  const g3  = add('guide:blur h2', 'boxBlur', [gab], 'RG32Float', 'guide', '1/4');
  const gV2 = add('guide:blur v2', 'boxBlur', [g3], 'RG32Float', 'guide', '1/4');

  // ── Masks: a fold of 8 components, each with its own guided refine ──
  const MC = 8, comp = [], refine = [];
  const mBase = add('mask:base', 'maskBase', [], 'R16Float', 'masks');
  const accum = auxTex('brush accumulator', 'R32Float', 'masks', '1 × 1 until the first dab, then full res');
  let prev = mBase;
  for (let i = 0; i < MC; i++) {
    const a = auxTex('mask ' + i + ' matte and dabs', 'R16Float + RGBA32Float', 'masks', 'matte ≤ 1024 px, 256 × 64 dabs, 256 bounds');
    comp[i] = add('mask:' + i, 'maskComponent', [prev, hs], 'R16Float', 'masks', 'full res', [a, accum]);
    prev = comp[i];
  }
  for (let i = 0; i < MC; i++) {
    const t = 'mask' + i + ':';
    const p  = add(t + 'guide prep', 'maskGuidePrep', [gp, comp[i]], 'RGBA32Float', 'refine', '1/4');
    const b1 = add(t + 'guide blur h', 'boxBlur4', [p], 'RGBA32Float', 'refine', '1/4');
    const b2 = add(t + 'guide blur v', 'boxBlur4', [b1], 'RGBA32Float', 'refine', '1/4');
    const ab = add(t + 'guide coeffs', 'maskGuideAb', [b2], 'RG32Float', 'refine', '1/4');
    const b3 = add(t + 'guide blur h2', 'boxBlur', [ab], 'RG32Float', 'refine', '1/4');
    const b4 = add(t + 'guide blur v2', 'boxBlur', [b3], 'RG32Float', 'refine', '1/4');
    refine[i] = add(t + 'refine', 'maskGuideApply', [comp[i], b4, gp], 'R16Float', 'refine');
  }

  // ── Output ──
  const linear = add('develop:linear', 'developLinear', [fusion, gV2, gp, ...refine], 'RGBA16Float', 'tone');
  const grade  = add('grade + vignette', 'colorGrade', [linear], 'RGBA16Float', 'grade');
  const auxCurve = auxTex('tone curve LUT', 'R32Float', 'display', 'curve rows');
  const auxCube  = auxTex('creative LUT', 'RGBA32Float', 'display', '65³ grid at most');
  const display  = add('develop:display', 'developDisplay', [grade], 'RGBA16Float', 'display', 'full res', [auxCurve, auxCube]);
  const auxPlate = auxTex('grain plate', 'R32Float', 'grain', 'stacked mip chain, uploaded once');
  const grain    = add('develop:grain', 'grain', [display], 'RGBA8Unorm', 'grain', 'full res', [auxPlate]);
  add('geometry', 'geometry', [grain], 'RGBA8Unorm', 'geometry', '1.45 × long edge, square');

  const NOTES = {
    'mask:base': 'No inputs. Writes zero, the identity the mask fold starts from. Its parameters never change, so it runs once per photograph.',
    'hl:fill': 'Its first input is the picture, so with the fill switched off it passes the highlights output straight through.',
    'clarity': 'Its first input is the dehaze output, so clarity at zero passes straight through and its 36-node chain costs nothing.',
    'fusion': 'Its first input is the clarity output, so fusion at zero passes straight through.',
    'dehaze': 'Its first input is the profile output, so dehaze at zero passes straight through.',
    'develop:linear': 'Every scene-linear adjustment in one dispatch: exposure, whites, blacks, highlights, shadows, saturation, vibrance, HSL and every mask layer. It has 11 inputs.',
    'geometry': 'The last node, and the only one whose output size differs from its input. It is allocated for the worst-case straighten, so rotating never recompiles the graph.',
    'brush accumulator': 'One texture shared by whichever mask component is being painted. Registered at 1 × 1 and grown on the first dab.',
    'develop:grain': 'Also dithers and quantizes to 8 bits, so the grain lands on unquantized values.',
    'sharpen': 'Upstream of every tone control, so dragging exposure never re-runs it.',
    'profile:hue/sat': 'The fan-out point. Dehaze, the guide, every mask component and everything after them read this node.',
  };
  for (let i = 0; i < MC; i++) NOTES['mask' + i + ':refine'] = 'The mask is the first input, so a disabled refine passes the raw coverage through.';

  // Sliders → the nodes their apply*() writes parameters into (DevelopPipeline::apply).
  const L = (...ls) => ls.map(l => byLabel[l]);
  const startsWith = p => N.filter(n => n.label.startsWith(p)).map(n => n.id);
  const inStage = s => N.filter(n => n.stage === s).map(n => n.id);
  const SLIDERS = [
    { name: 'White balance', fn: 'applyWhiteBalance', ids: L('linearize'), note: 'Pushed into the head of the graph, so every node recomputes. The most expensive control by far.' },
    { name: 'Highlight recovery', fn: 'applyHighlights', ids: L('highlights', 'hl:mask', 'hl:fill') },
    { name: 'Denoise', fn: 'applyDenoise', ids: startsWith('denoise:') },
    { name: 'Lens corrections', fn: 'applyLens', ids: L('lens') },
    { name: 'Spot removal', fn: 'applySpots', ids: L('spots:measure', 'spots:apply') },
    { name: 'Sharpening', fn: 'applySharpen', ids: L('sharpen') },
    { name: 'Dehaze', fn: 'applyDehaze', ids: inStage('dehaze') },
    { name: 'Clarity', fn: 'applyClarity', ids: inStage('clarity') },
    { name: 'Exposure fusion', fn: 'applyFusion', ids: inStage('fusion') },
    { name: 'Exposure, whites, blacks', fn: 'applyTone', ids: L('develop:linear'), note: 'Placed after every local operator on purpose. Exposure is a multiply, so dehaze, clarity, fusion and the guide all stay cached while it moves.' },
    { name: 'Highlights and shadows', fn: 'applyTone', ids: L('develop:linear'), note: 'While dragging, only develop:linear re-runs. Moving off zero also switches on the guided filter chain (applyGuide), which runs once.' },
    { name: 'Saturation, vibrance, HSL', fn: 'applyTone', ids: L('develop:linear') },
    { name: 'Color grading and vignette', fn: 'applyGrade', ids: L('grade + vignette') },
    { name: 'Contrast, curve, LUT', fn: 'applyOutput', ids: L('develop:display', 'tone curve LUT', 'creative LUT') },
    { name: 'Grain', fn: 'applyOutput', ids: L('develop:grain') },
    { name: 'Crop, rotate, straighten', fn: 'applyGeometry', ids: L('geometry', 'grade + vignette'), note: 'The vignette follows the crop, so the grade re-runs too.' },
    { name: 'Mask gradient or brush', fn: 'applyMaskComponents', ids: L('mask:0', 'mask 0 matte and dabs') },
    { name: 'Mask edge refine', fn: 'applyMaskRefine', ids: startsWith('mask0:') },
  ];

  const byId = Object.fromEntries(N.map(n => [n.id, n]));
  const readers = Object.fromEntries(N.map(n => [n.id, []]));
  for (const n of N) for (const s of new Set([...n.inputs, ...n.aux])) readers[s].push(n.id);
  function walk(starts, next) {
    const seen = new Set(), stack = [...starts];
    while (stack.length) { const x = stack.pop(); for (const y of next(x)) if (!seen.has(y)) { seen.add(y); stack.push(y); } }
    return seen;
  }
  // Pipeline::markDownstreamDirty: everything that consumes a changed node, transitively.
  const downstream = ids => walk(ids, x => readers[x]);
  const upstream = ids => walk(ids, x => [...byId[x].inputs, ...byId[x].aux]);

  window.OrionGraph = { REGIONS, STAGES, N, byId, byLabel, readers, downstream, upstream, SLIDERS, NOTES };
})();
