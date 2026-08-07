# ANI-402 comparison (first 10 instances)

[← Documentation index](README.md) · [See also: full ANI-201 comparison](ani201-full-comparison.md)

First 10 instances of the paper's ANI-402 benchmark family (400 items,
larger and harder than ANI-201), `bpp-solve INSTANCE --root-cg` (default
flags, no tuning) against the official BCCF reference binary. Same
machine, same run, same methodology as the ANI-201 comparison.

## Summary

| Metric | Value |
|---|---|
| Instances certified (ours) | **10/10** |
| Official binary timeouts (180s) | 1 (`NR_3`) — ours certified it in 65.7s |
| Mean time ratio (ours / official, excl. the timeout) | **5.01x** |
| Median time ratio | 3.41x |
| Best ratio | 0.55x (`NR_6`, ours faster in absolute terms) |
| Worst ratio | 10.43x (`NR_1`) |
| Official mean time (excl. timeout) | 28.94s |
| Ours: mean / min / max | 67.99s / 50.98s / 83.12s |

## Same consistency pattern as ANI-201, more pronounced

The official binary's time on this sample ranges from **6.4s to a full
180s timeout** — a ~28x spread depending on instance difficulty. This
solver stays in a **51s-83s** band throughout, a ~1.6x spread. The
practical consequence is sharper here than on ANI-201: on `NR_3`, the
official binary did not certify within 180s at all, while this solver
did, in 65.7s — well inside its normal range for this family. On `NR_6`
this solver is also faster in absolute terms (51.0s vs. 92.0s).

The overall mean ratio (5.01x) is wider than ANI-201's (2.50x), consistent
with ANI-402 being the larger, harder family (400 items vs. 201,
capacity ~10000 vs. ~2456) — see `docs/ROADMAP.md` for what is and is not
yet optimized for this scale specifically (this comparison is the first
one run against the current default-flags baseline; earlier ANI-402
measurements in `docs/STATUS.md` predate the Algorithm 1 and SAFE_MIP_SOL
work).

## Full results

| Instance | Official (s) | Ours (s) | Ratio | UB | Certified |
|---|---|---|---|---|---|
| `402_10000_NR_0` | 22.77 | 77.62 | 3.41x | 133 | yes |
| `402_10000_NR_1` | 6.42 | 61.18 | 9.53x | 133 | yes |
| `402_10000_NR_2` | 48.28 | 74.41 | 1.54x | 133 | yes |
| `402_10000_NR_3` | **timeout (180s)** | 65.70 | — | 133 | yes (official did not finish) |
| `402_10000_NR_4` | 6.83 | 71.29 | 10.43x | 133 | yes |
| `402_10000_NR_5` | 12.74 | 71.22 | 5.59x | 133 | yes |
| `402_10000_NR_6` | 91.96 | 50.98 | 0.55x | 133 | yes |
| `402_10000_NR_7` | 7.04 | 66.40 | 9.43x | 133 | yes |
| `402_10000_NR_8` | 36.79 | 57.99 | 1.58x | 133 | yes |
| `402_10000_NR_9` | 27.65 | 83.12 | 3.01x | 133 | yes |

## What's still open

- Only the first 10 of ANI-402's 50 instances tested so far; the
  remaining 40 are the natural next step, matching the ANI-201 full
  sweep.
- The mean-case gap on this family (5.01x) is wider than ANI-201's
  (2.50x) and not yet decomposed further — whether this is dominated by
  the same per-iteration costs already identified on ANI-201 (see
  `docs/STATUS.md`) or something specific to the larger instance size
  (capacity, item count) is not yet determined.
