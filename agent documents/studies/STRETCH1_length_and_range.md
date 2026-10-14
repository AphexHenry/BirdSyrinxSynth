# STRETCH1 — Length and pitch range on a dozen syllables

> A listening tool, not a blind test (WP-R3, feedback round). Status: **done 2026-10-14**.
> Page: `listening/R3/stretch1/index.html` · generator: `scripts/studies/stretch1_make.py` (template `scripts/studies/stretch1_page.html`) · design: `listening/R3/stretch1/design.json` (hash `45656e147bf5`) · answers: `studies/data/STRETCH1/answers.json`.
> Origin: VARY2 showed that the shape model, without its loudness changes, cannot be heard. The human: "variations are not critical. a little bit would be enough. let's try a last one. let's work only with syllable duration, going from 80% to 130%, and with frequency amplitude around the note. basically compressing or extending the variation of frequency around the note, also from 80% to 130%. make me a test so i hear the differences on a dozen syllables". Then: "add a bit of variation. let's go from 60 to 200%, same for pitch amplitude".

## Method

- **12 syllables** of different kinds: 7 from 22938, 5 from 22937 (falling / rising swipes, peeps, swipe+swipe, Hswipe, Lswipe, Lpeep, one long call). The whistle is synthesised in the page.
- **Length:** the curve stretched in time to 60 / 80 / 100 / 130 / 160 / 200 %; the pitches stay the same.
- **Pitch range:** the pitch movement around the syllable's note scaled by 60 … 200 %, note + k·(pitch − note). The note is the bank's: the loudness-weighted pitch, or the note set by ear.
- **Random draws** of both, log-uniform in 60–200 % (shorter and longer equally likely), also on two real phrases in context. The bird's real gaps are kept, so a longer syllable pushes the next one later.
- The listener ticks syllables that sound unnatural and writes the spans they like.

## Results (the human, 2026-10-14)

- **Spans chosen:** length **80–160 %**, pitch range **60–160 %**. No syllable was ticked as unnatural.
- **Comment** (verbatim): "swipes could actually get a lenght stretch up to 200%. make sure to do the pitch range around the syllable pitch. some sounds sound higher the bigger the range. but i suppose it could be caused simply by a syllable which analysed pitch could be off, as it happen sometimes."
- **Then:** "that's working nicely."

## What it decided (D-036, amended)

The plugin's Variation is now this:
- each syllable at a random length (80–160 %; labels containing "swipe" up to 200 %) and pitch range (60–160 %);
- log-uniform draws; Variation v scales the log spans (0 = the real syllables, 1 = these spans);
- the next syllable waits for a longer one (real gaps kept).

The range scales around the syllable's note, as asked. Two likely causes for "sounds higher the bigger the range":
- a short high spike above a lower, louder body: widening raises the spike much more, and the ear follows it;
- tracker jumps (bank flag "hop", 223 syllables): these syllables are now only narrowed, never widened.
