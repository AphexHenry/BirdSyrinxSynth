# WP-<ID> report — <title>

| Field | Value |
|---|---|
| WP | WP-<ID> |
| Status | in progress / awaiting approval / partial / done |
| Started | YYYY-MM-DD |
| Finished | YYYY-MM-DD |
| Agent | (model / tool) |
| Sessions | 1 (add a line per resumed session) |

## 1. Summary (plain language, 3–6 lines)

What exists now that did not exist before, and what it sounds like.

## 2. What was built

| File / module | Purpose |
|---|---|
| `path/to/file` | … |

## 3. How to run it

```bash
# exact commands to build, test, render, open the listening page
```

## 4. Acceptance criteria

| # | Criterion (from the WP) | Result | Evidence (numbers, test name, file) |
|---|---|---|---|
| 1 | … | PASS / FAIL / NOT DONE | … |

## 5. Quality gate

```
<last lines of scripts/check.sh output>
```

## 6. Listening & results

- Page: `listening/<WP-ID>/index.html`
- Items and what to listen for:

| File | What to listen for | Key metrics |
|---|---|---|
| `01_….wav` | … | … |

Key figures (copied to `agent documents/reports/assets/<WP-ID>/`):

![description](assets/<WP-ID>/figure.png)

## 7. Deviations from the spec

| Spec said | What I did | Why |
|---|---|---|

## 8. Contract changes (see decisions.md)

- None / D-0xx (proposed): …

## 9. Measurements & baselines

| Metric | Value | Conditions |
|---|---|---|

## 10. Known issues & limitations

- …

## 11. Tools / dependencies added

- …

## 12. Handoff notes for the next agent

- Where things are, how to use them, what is fragile, what I would do next.

## 13. Partial — remaining work (only if status is partial)

- [ ] …

## 14. Revision history (feedback rounds)

| Date | Human feedback | Changes |
|---|---|---|

## 15. Proposed commit message

```
WP-<ID>: <title>

<summary lines>
Report: agent documents/reports/WP-<ID>_report.md
```
