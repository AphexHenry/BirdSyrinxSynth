# WP-R2 — Anatomical engine as an optional voice (click fix and colour) — OPTIONAL

| Field | Value |
|---|---|
| Phase | R · Depends on R1 · Size M · status **optional** (the human: "switch to what works, even if non-anatomical") |

**Goal.** Make the Tier-1 anatomical engine usable as an **optional** voice next to the whistle, and only promote it if it rates as high as the whistle against the real recording.

**What is known** (`agent documents/studies/ENGINE1_engine_cleanup.md`):
- **Parameters alone** (tone 0.6, aspiration off, radiation 1, attack 3 / release 8 ms, default tract) fix the breath and most of the brightness (H2 −27 to −29 dB vs the bird's −30), but **not the clicks**.
- **Clicks** come from the bifurcation switching the oscillation on and off at full level: the labia jump into vibration, or snap back to rest, at every syllable boundary. They are worst at syllable ends, and partly inside the bird's band (up to about 4 kHz), so a filter does not fix them.
- **Proven fix (offline):** keep the pressure constant during a syllable; start the oscillation **12 ms early** and stop it **10 ms late**; apply the gesture's volume envelope as an output gain. All click measures then reach the real bird's level.
- **Listening:** the fixed engine scored 100 on passage P1, but **52 on P3** (long alarm and low swipes). The whistle scored 100 on both.

**Work:**
1. Implement the articulation fix in `VoiceController` / `BirdVoice`: a new per-sample output-gain control, pre-roll / post-roll of the oscillation, and gaps shorter than the pre-roll + post-roll handled by keeping the oscillation running. This needs a `proposed` contract decision.
2. Find why P3 fails: compare the engine and the whistle on the alarm (≈ 23.45–23.95 s, 27.6–28.0 s) and on low swipes. Candidates: harmonic brightness at 2.7–3 kHz, contour wobble on the rough alarm, onset regime.
3. Re-test with the MUSHRA page against the whistle. Promote the engine to default only if it is within 10 points of the whistle on all passages.
