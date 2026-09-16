# Syrinx — agent documents (read me, human)

This folder contains everything a coding agent needs to build the anatomical songbird synthesizer step by step, and everything you need to steer it.

## What is in here

| File | For | Content |
|---|---|---|
| `protocol.md` | the agent | how to pick a task, work, report, and wait for your approval |
| `state.md` | both | live status of every work package, current capabilities, open questions |
| `01_research.md` | both | Part 1: anatomy → DSP modules, equations, playability concept, fitting concept, references |
| `02_architecture.md` | the agent | tech stack, repo layout, APIs, parameter list, file formats, tests, listening kit |
| `03_plan.md` | both | Part 2 overview: phases, order, dependency graph, milestones |
| `work_packages/` | the agent | one detailed spec per task (27 files) |
| `decisions.md` | both | decision log (some entries need you: D-003 licence) |
| `backlog.md` | both | ideas for later |
| `reports/` | both | one report per finished task (+ `_TEMPLATE.md`) |

## Your loop (one task at a time)

1. Put this folder at the project root as `agent documents/`. (Before WP-D1, copy your zebra finch recordings and annotation files into `data/raw/zebra_finch/` inside the project.)
2. In Cursor, open the project folder, start a **new chat** with the agent and type:
   **`do as instructed in protocol.md`**
   (Tip: typing `@protocol.md` attaches the file directly.)
3. The agent announces which task it takes and works. It may ask you questions.
4. At the end it opens a **listening page** in your browser (`listening/<task>/index.html`) and tells you what to listen for.
5. Listen and look. Then reply either:
   - **`approve`** → it commits the task and stops, or
   - your feedback → it fixes and asks again.
6. Close the chat. Start a new chat for the next task (back to step 2).

## Milestones worth a longer listening session

- **M1** after B3: first complete bird sound
- **M2** after B6: complete voice (two sides, breath, size)
- **M3** after C3: playable in your DAW from MIDI, in tune
- **M4** after C6: the singing instrument with macros and presets
- **M5** after E4: real zebra finch vs resynthesis
- **M6** after F3: the plugin sings with variations learned from your recordings
- **M7** after G1: release candidate

At a milestone you can change the plan: edit `state.md` (order, `optional`/`todo`), edit a work package file, and write a line in `decisions.md`.

## If something goes wrong

- The agent seems lost or out of scope → tell it: "stop, re-read protocol.md §0 and your WP file".
- A chat crashed mid-task → just start a new chat with the same prompt; the protocol resumes the task from its report.
- You want to skip a task → set its status to `optional` in `state.md`.
- You want to redo a task → set it to `todo` and describe what to change in the "Open questions" section of `state.md`.
