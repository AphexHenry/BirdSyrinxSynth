# protocol.md — instructions for the coding agent

You are a coding agent working on **Syrinx** (working title), an anatomy-based songbird synthesizer plugin (VST3/AU/CLAP) for **musicians**, by Cuicui Instruments.
The human you work with is a musician/music producer with a scientific background, not a professional C++ developer. They open **one new chat per work package** and always start with the same prompt: *"do as instructed in protocol.md"*.

**Read this whole file before doing anything else. Follow it literally.**

All project documents live in the folder `agent documents/` at the project root. **The folder name contains a space — always quote paths** (e.g. `"agent documents/state.md"`).

---

## 0. Golden rules

1. **One work package (WP) per chat.** Do exactly one WP: the one selected by §3. Never start the next one.
2. **Never commit without explicit human approval** in this chat (§6). Never push unless asked.
3. **Stay in scope.** Do what the WP file asks. Ideas and extra features go into `agent documents/backlog.md`.
4. **Contracts are protected.** Parameter IDs, public APIs, file formats, repo layout and tooling defined in `02_architecture.md` change only through a `proposed` entry in `decisions.md` and a mention in your report (§5.6).
5. **Evidence over claims.** Every acceptance criterion in your report has a number, a test name, or a file you produced. "Should work" is not acceptable.
6. **Leave a trail.** Create your report file at the beginning and update it while you work, so an interrupted session can be resumed by the next agent.
7. **The human listens at the end.** Produce the listening page (§5.3) before asking for approval. Say concretely what to listen for.
8. **Keep the build green.** `scripts/check.sh` must pass before you ask for approval (from WP-A1 on).
9. **No silent failures.** If something does not work, say so in the report and in your final message.
10. **Ask when blocked** (§8). Do not guess on things that are the human's decision.
11. **Real-time safety** in audio code is mandatory (`02_architecture.md` §5.1, `01_research.md` §6.3).
12. **Never commit raw recordings** or anything under `data/raw/`, `data/interim/`, `listening/`, `build/`, `tools/`.

---

## 1. Document map

| File | What | When to read |
|---|---|---|
| `agent documents/protocol.md` | this file | always, first |
| `agent documents/state.md` | live status of all WPs, current capabilities, known issues, session log | always, second |
| `agent documents/work_packages/WP-<ID>_*.md` | the spec of your WP | always, fully |
| `agent documents/01_research.md` | anatomy, equations, playability and fitting concepts | the sections listed in your WP's "Read first" |
| `agent documents/02_architecture.md` | stack, repo layout, APIs, parameter registry, file formats, tests, listening kit, git policy | the sections listed in your WP's "Read first"; §4 (scripts) and §12 (git) always |
| `agent documents/03_plan.md` | phases, order, dependency graph | skim once |
| `agent documents/decisions.md` | decision log (accepted/proposed/open) | read all `accepted` and `open` entries |
| `agent documents/backlog.md` | ideas for later | only to append |
| `agent documents/reports/WP-<ID>_report.md` | reports of finished WPs | the reports of your WP's dependencies, at least their "Handoff notes" |
| `agent documents/reports/_TEMPLATE.md` | report template | when creating your report |

---

## 2. Session start

Do these steps in order and keep the chat output short.

**2.1 Orient.**
- Find the project root (the folder containing `agent documents/`). Work from there.
- Read `agent documents/state.md`.
- Run `git status --short` (if the project is not yet a git repository, that is expected before WP-A1 is done).

**2.2 Check for unfinished business.**

| Situation | Action |
|---|---|
| A WP has status `awaiting approval` and there are uncommitted changes | Tell the human which WP is awaiting approval and ask: approve (→ §6), give feedback, or discard? Do nothing else until they answer. |
| A WP has status `in progress` (previous chat interrupted) | Read its report draft, check the files it lists, tell the human you are resuming it, then continue that WP from where the report says. |
| A WP has status `partial` | That WP is your WP: continue it (read the "Remaining work" section of its report). |
| Uncommitted changes that no status explains | Stop. Show `git status --short` to the human and ask what to do. |
| Nothing unfinished | Go to 2.3. |

**2.3 Select the WP** using the rules in §3.

**2.4 Prepare.**
- Read the WP file completely, then its "Read first" sections, then the reports of its dependencies (at least "Handoff notes" and "Known issues"), and `decisions.md` (accepted + open entries).
- Run `scripts/doctor.sh` (if it exists) to check the environment.
- Create your report: copy `agent documents/reports/_TEMPLATE.md` to `agent documents/reports/WP-<ID>_report.md` and fill the header.
- In `state.md`: set your WP's status to `in progress`, fill "Updated" with today's date, and add a line to the session log.

**2.5 Announce (short).** Post one message to the human with: the WP ID and title, a 3–6 bullet plan, anything you need from them (e.g. the location of recordings). If nothing blocks you, **continue working without waiting** for an answer.

---

## 3. Selecting the WP

1. If a WP is `in progress` or `partial`, that is your WP (§2.2).
2. Otherwise take the **first row** of the execution table in `state.md` whose status is `todo` **and** whose dependencies all have status `done`.
3. Rows with status `optional`, `blocked`, `awaiting approval` or `done` are never selected automatically. (The human turns an `optional` WP into `todo` when they want it.)
4. If no row qualifies, tell the human why (e.g. "C3 waits for C1 which is blocked") and stop.
5. If the human explicitly names a WP in the chat, use that one (still check dependencies and warn if they are not done).

---

## 4. While working

- **Small steps.** Implement → build → test → commit nothing → repeat. Run the relevant tests often; run `scripts/check.sh` at least before finishing.
- **Follow the WP's "Suggested steps"** unless there is a good reason not to; record deviations in the report ("Deviations from the spec" with the reason).
- **Update the report as you go** (at least after each major step): what exists, what works, what doesn't.
- **Long-running commands** (atlas generation, fitting): run them with logs written to `build/logs/<WP-ID>_<name>.log`, make them resumable, and report the time they took.
- **Stuck?** If the same problem survives 3 genuinely different attempts (or roughly 45 minutes), stop trying: write what you tried in the report and ask the human (§8), or choose a documented fallback if the WP offers one.
- **Other documents.** Do not rewrite `01_research.md`, `02_architecture.md`, `03_plan.md` or other WPs' specifications. You may:
  - fix obvious typos,
  - append a section `## Notes from WP-<ID>` at the end of a *later* WP file when you discover something the next agent must know (e.g. "the atlas header is at …").
- **Contracts.** If you must change a contract, write a `proposed` entry in `decisions.md` and list it in the report under "Contract changes". Do not change it silently.
- **Scope creep.** Tempting improvement not in the WP? One line in `backlog.md` (with your WP ID), then move on.
- **Dependencies/tools.** New library or tool → justify in the report; keep the dependency list in `02_architecture.md` §1 up to date via a `proposed` decision if it is a new *kind* of dependency.

---

## 5. Finishing a WP (in this order)

**5.1 Acceptance.** Go through every item in the WP's "Tests & acceptance criteria". In the report, mark each **PASS / FAIL / NOT DONE** with evidence (numbers, test names, file paths).

**5.2 Quality gate.** Run `scripts/check.sh`. Paste its final summary lines in the report. It must pass. If it cannot pass for a reason outside your control, explain in detail and flag it in the final message.

**5.3 Listening & results page.**
- Render everything listed in the WP's "Listening & results" section into `listening/<WP-ID>/` with a `manifest.json` (`02_architecture.md` §11).
- Build the page: `scripts/listen.sh <WP-ID>` → `listening/<WP-ID>/index.html`.
- On macOS, open it for the human: `open "listening/<WP-ID>/index.html"`.
- Copy at most 10 small key images to `agent documents/reports/assets/<WP-ID>/` and reference them in the report.
- (Before WP-A1 finishes the listening kit does not exist; WP-A1 builds it.)

**5.4 Report.** Complete every section of the report template. The "Handoff notes" section is written for the *next agent*: where things are, how to run them, what is fragile.

**5.5 State.** Update `state.md`:
- your WP status → `awaiting approval`,
- "Current capabilities" (what the product can do now, in plain words),
- "Baselines" (e.g. CPU per voice, tuning error) if your WP measured them,
- "Known issues" (add/remove),
- "Open questions for the human" (if any),
- session log line.

**5.6 Decisions & backlog.** Add `proposed` decisions and backlog items if any.

**5.7 Prepare the commit (do not commit).**
- `git add -A` (the `.gitignore` protects data and builds; check that no large or forbidden file is staged: `git diff --cached --stat`).
- Write the proposed commit message at the end of the report (format in `02_architecture.md` §12).

**5.8 Final message to the human.** Use this structure, in plain language (no unexplained jargon):

```
WP-<ID> — <title>: ready for your review

What I built
- …(3–6 bullets)

Listen / look
- Open: listening/<WP-ID>/index.html  (already opened in your browser)
- Listen for:
  1. …
  2. …

Results
- <key numbers: PASS/FAIL summary>

Known issues / limitations
- …

Questions for you (optional)
- …

Reply "approve" to commit this WP, or tell me what to change.
```

Then **stop and wait**.

---

## 6. Approval and feedback

| Human reply | Your action |
|---|---|
| **approve** (or clear equivalent: "approved", "ok, commit", "go") | 1) In `state.md` set status `done`, set "Tag" to `wp-<ID>`, update date. 2) `git add -A` again (state/report changed). 3) `git commit` with the prepared message. 4) `git tag wp-<ID>`. 5) Reply with the commit hash and the next WP in line (from `state.md`). **Do not start it.** |
| **feedback** (changes requested) | Apply the changes within the same WP. Add an entry to the report's "Revision history". Re-run §5.1–5.3, update §5.4–5.7, and send a new final message (§5.8) that starts with what changed. |
| **commit partial** / "stop here" | Follow §7, then commit only after the human confirms. |
| **discard** | Ask for confirmation, then `git restore`/`git clean` only the files of this WP (list them first), set status back to `todo`, log it in the session log. |

---

## 7. Partial completion (large WPs)

If the WP is too big for one session (the conversation is getting very long, or the remaining work is clearly more than a few hours), stop at a **coherent checkpoint** where the build is green:
- report section "Partial — remaining work" with a precise checklist of what is left,
- status `partial` in `state.md`,
- listening page for what exists,
- final message asking to commit the partial state ("Reply 'approve' to commit this partial state").
The next chat will continue the same WP (§2.2).

---

## 8. When to stop and ask the human

- A decision belongs to the human: product naming, licensing, musical/aesthetic choices not specified, changing the order of WPs, deleting data.
- Something needs the human physically: installing Xcode, accepting licences, providing recordings or annotation files, granting permissions, using a DAW.
- An acceptance criterion seems wrong or impossible (explain why, propose an alternative criterion).
- A contract change is needed that affects more than your WP.
- You are stuck (§4).
- Data location or format differs from what the WP expects.

When you ask, give **options with your recommendation**, so the human can answer in one line.

---

## 9. Definition of done (every WP)

- [ ] All acceptance criteria PASS (or FAIL/NOT DONE explicitly accepted by the human).
- [ ] `scripts/check.sh` passes.
- [ ] New code has tests; audio code is real-time safe.
- [ ] Listening/results page built and opened; "listen for" notes written.
- [ ] Report complete (incl. handoff notes, deviations, contract changes, commit message).
- [ ] `state.md`, `decisions.md`, `backlog.md` updated.
- [ ] No forbidden files staged.
- [ ] Human approved → committed and tagged.

---

## 10. Communication style

- English. Short, concrete, friendly.
- The human is an expert listener: give precise listening instructions ("at 0:02 the pitch should jump without a glide"), not generic ones.
- Explain technical terms the first time in a few words.
- Never overstate: if something sounds bad or is unverified, say it.

---

## 11. Practical notes (Cursor on macOS)

- Use the integrated terminal; run commands from the project root.
- Quote paths with spaces: `"agent documents/…"`.
- Prefer `uv run …` for Python commands (`cd python && uv run pytest` or as defined by `scripts/test.sh`).
- Build outputs go to `build/`; logs to `build/logs/`.
- Do not open or print huge files (audio, binary tables, big JSON) into the chat; summarise them.
- If a command needs more than ~10 minutes, run it in the background with a log file and poll the log.
- Recordings live in `data/raw/zebra_finch/` (the human puts them there; see WP-D1).
