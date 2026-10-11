// PITCH2 shared synthesis and placement (inlined into index.html and playground.html by pitch2_make.py).
// Pitches are absolute cents: 6900 = A4, 100 cents per tempered semitone (cents / 100 = MIDI note).
(function () {
  "use strict";
  const BIRD_RMS = 0.08;       // about -22 dBFS on active samples
  const PAD_GAIN_DB = -6;      // pad RMS relative to the bird
  const BIRD_DELAY_S = 0.35;   // pad starts first, bird enters after
  const PAD_TAIL_S = 0.45;

  const hzOf = (c) => 440 * Math.pow(2, (c - 6900) / 1200);
  const median = (xs) => { const s = xs.slice().sort((a, b) => a - b); const m = s.length >> 1; return s.length % 2 ? s[m] : 0.5 * (s[m - 1] + s[m]); };

  // Shift (cents) applied to each syllable so that its `rule` anchor lands on target + delta.
  // snap = 1: every syllable on the note; snap = 0: the phrase keeps its natural melody around the note.
  function shifts(material, rule, delta, target, snap) {
    const an = material.syllables.map((s) => s.anchors[rule]);
    const centre = median(an);
    return an.map((a) => (target + delta) + (1 - snap) * (a - centre) - a);
  }

  // "Own height": the note name `pitchClass` (0 = C … 11 = B) at the octave nearest the material's natural height.
  function ownHeightTarget(material, rule, pitchClass) {
    const centre = median(material.syllables.map((s) => s.anchors[rule]));
    let best = null;
    for (let oct = 4; oct <= 10; oct++) {
      const c = (12 * (oct + 1) + pitchClass) * 100;  // MIDI (12*(oct+1)+pc) in cents
      if (best === null || Math.abs(c - centre) < Math.abs(best - centre)) best = c;
    }
    return best;
  }

  function rmsActive(d, env) {
    let peak = 0;
    for (let i = 0; i < env.length; i++) if (env[i] > peak) peak = env[i];
    let s = 0, n = 0;
    for (let i = 0; i < d.length; i++) if (env[i] >= 0.1 * peak) { s += d[i] * d[i]; n++; }
    return n ? Math.sqrt(s / n) : 0;
  }

  // Bird: one sine per syllable following its contour, shifted per syllable; real timing kept.
  function renderBird(sr, material, sh, out, env, offset) {
    material.syllables.forEach((s, k) => {
      const factor = Math.pow(2, sh[k] / 1200);
      const F = s.f_hz, A = s.a, m = F.length;
      const start = offset + Math.round(s.t0_ms / 1000 * sr);
      const n = Math.round(m / 1000 * sr);
      let ph = 0;
      for (let i = 0; i < n; i++) {
        const tms = i / sr * 1000;
        let j = Math.floor(tms), fr = tms - j;
        if (j >= m - 1) { j = m - 2; fr = 1; }
        const f = Math.exp(Math.log(F[j]) * (1 - fr) + Math.log(F[j + 1]) * fr) * factor;
        const a = A[j] * (1 - fr) + A[j + 1] * fr;
        ph += 2 * Math.PI * f / sr;
        if (ph > 2 * Math.PI) ph -= 2 * Math.PI;
        const idx = start + i;
        if (idx >= 0 && idx < out.length) { out[idx] += a * Math.sin(ph); env[idx] = Math.max(env[idx], a); }
      }
    });
  }

  // Pad: soft held note, harmonics 1..12 at 1/n^2, 80 ms attack, 200 ms release.
  function renderPad(sr, midi, n) {
    const f0 = hzOf(midi * 100);
    const d = new Float32Array(n);
    const att = Math.round(0.08 * sr), rel = Math.round(0.2 * sr);
    const H = [];
    for (let h = 1; h <= 12 && h * f0 < sr / 2 - 1000; h++) H.push([h, 1 / (h * h)]);
    for (let i = 0; i < n; i++) {
      let s = 0;
      const t = i / sr;
      for (const [h, g] of H) s += g * Math.sin(2 * Math.PI * h * f0 * t + h);
      let e = 1;
      if (i < att) e = 0.5 - 0.5 * Math.cos(Math.PI * i / att);
      else if (i > n - rel) e = 0.5 - 0.5 * Math.cos(Math.PI * Math.max(0, n - i) / rel);
      d[i] = s * e;
    }
    return d;
  }

  // Full trial: pad (optional) + bird. Returns an AudioBuffer.
  function renderTrial(ctx, material, opts) {
    const sr = ctx.sampleRate;
    const sh = shifts(material, opts.rule, opts.delta, opts.target, opts.snap === undefined ? 1 : opts.snap);
    const birdOffset = Math.round(BIRD_DELAY_S * sr);
    const n = birdOffset + Math.round((material.dur_ms / 1000 + PAD_TAIL_S) * sr);
    const bird = new Float32Array(n), env = new Float32Array(n);
    renderBird(sr, material, sh, bird, env, birdOffset);
    const gb = BIRD_RMS / (rmsActive(bird, env) || 1);
    const buf = ctx.createBuffer(1, n, sr);
    const d = buf.getChannelData(0);
    for (let i = 0; i < n; i++) d[i] = bird[i] * gb * (opts.birdOn === false ? 0 : 1);
    if (opts.padOn !== false) {
      const pad = renderPad(sr, opts.refMidi, n);
      let s = 0;
      for (let i = 0; i < n; i++) s += pad[i] * pad[i];
      const gp = BIRD_RMS * Math.pow(10, PAD_GAIN_DB / 20) / (Math.sqrt(s / n) || 1);
      for (let i = 0; i < n; i++) d[i] += pad[i] * gp;
    }
    return buf;
  }

  window.P2 = { shifts, ownHeightTarget, renderTrial, hzOf, median };
})();
