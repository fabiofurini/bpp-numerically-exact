# ANI-201 full comparison (50/50 instances)

[← Documentation index](README.md)

Full-sample comparison of `bpp-solve INSTANCE --root-cg` (default flags,
no `--populate`/`--branch-price`/tuning options) against the official
BCCF reference binary, on every one of the 50 instances in the paper's
ANI-201 benchmark family. Same machine, same run.

## Methodology

- **Official binary**: `official-reference-BCCF/BCCF/BPPS INSTANCE
  param_test_BPP-non-IRUP-2exp44.txt 0 1 1 1` (the paper's own reference
  parameter file; `1 1 1` = `SAFE_MIP_SOL`/`SATURATED_TRIPLET`/
  `STRONG_BRANCHING_COMPLETE`, all on, matching the official run scripts).
- **This solver**: `bpp-solve INSTANCE --root-cg`, no other flags. As of
  this checkpoint, `--root-cg` with no extra options runs exactly the
  paper's algorithm by default: Algorithm 1's two-phase column
  generation, automatic SR3 separation, and the Section 4 MIP-based exact
  certification (previously gated behind `--populate`, now unconditional
  — see `docs/STATUS.md`'s "SAFE_MIP_SOL implemented" and follow-up
  checkpoints).
- 60s timeout per solver per instance (neither ever approached it).

## Summary

| Metric | Value |
|---|---|
| Instances certified | **50/50** |
| UB mismatches vs. official | **0** |
| Mean time ratio (ours / official) | **2.50x** |
| Median time ratio | 2.79x |
| Best ratio (fastest relative to official) | 0.21x (`NR_33`) |
| Worst ratio | 4.10x (`NR_43`) |
| Official: mean / total time | 4.42s / 221.2s |
| Ours: mean / total time | 5.79s / 289.3s |

## A notable pattern: consistency

The official binary's time ranges from **1.71s to 24.45s** across the 50
instances — it is fast on "easy" instances and visibly slower on harder
ones (`NR_20`, `NR_21`, `NR_33`, `NR_34`, `NR_38`, `NR_39`, `NR_13`,
`NR_19`, `NR_4` all take 8-24s). This solver's time instead stays in a
tight **4.7s-8.2s** band across the entire sample, regardless of which
instances are "hard" for the official binary. The practical effect: on
the 9 hardest instances for the official binary, this solver is **faster
in absolute terms** (ratio < 1.0x) despite being ~3x slower on average
across the easy majority. The likely explanation is architectural, not
incidental: the official binary's SR3/branching schedule scales with
instance difficulty, while this solver's default path resolves nearly
everything through the same bounded root mechanism (SR3 + SAFE_MIP_SOL)
regardless of difficulty, so its cost stays roughly constant.

## Full results

| Instance | Official (s) | Ours (s) | Ratio | UB | Certified |
|---|---|---|---|---|---|
| `201_2500_NR_0` | 1.73 | 6.52 | 3.77x | 66 | yes |
| `201_2500_NR_1` | 1.82 | 6.42 | 3.53x | 66 | yes |
| `201_2500_NR_2` | 1.72 | 6.72 | 3.91x | 66 | yes |
| `201_2500_NR_3` | 1.72 | 5.92 | 3.45x | 66 | yes |
| `201_2500_NR_4` | 8.63 | 5.72 | 0.66x | 66 | yes |
| `201_2500_NR_5` | 1.71 | 5.62 | 3.28x | 66 | yes |
| `201_2500_NR_6` | 2.32 | 5.11 | 2.21x | 66 | yes |
| `201_2500_NR_7` | 2.92 | 5.71 | 1.96x | 66 | yes |
| `201_2500_NR_8` | 2.21 | 7.52 | 3.40x | 66 | yes |
| `201_2500_NR_9` | 1.91 | 5.71 | 2.99x | 66 | yes |
| `201_2500_NR_10` | 2.01 | 5.62 | 2.79x | 66 | yes |
| `201_2500_NR_11` | 1.81 | 4.91 | 2.71x | 66 | yes |
| `201_2500_NR_12` | 1.71 | 5.22 | 3.05x | 66 | yes |
| `201_2500_NR_13` | 13.63 | 6.92 | 0.51x | 66 | yes |
| `201_2500_NR_14` | 1.81 | 5.61 | 3.10x | 66 | yes |
| `201_2500_NR_15` | 1.81 | 5.22 | 2.88x | 66 | yes |
| `201_2500_NR_16` | 1.91 | 5.22 | 2.73x | 66 | yes |
| `201_2500_NR_17` | 1.91 | 5.12 | 2.68x | 66 | yes |
| `201_2500_NR_18` | 1.82 | 6.02 | 3.31x | 66 | yes |
| `201_2500_NR_19` | 11.93 | 4.82 | 0.40x | 66 | yes |
| `201_2500_NR_20` | 20.64 | 6.72 | 0.33x | 66 | yes |
| `201_2500_NR_21` | 17.73 | 8.22 | 0.46x | 66 | yes |
| `201_2500_NR_22` | 2.02 | 6.02 | 2.98x | 66 | yes |
| `201_2500_NR_23` | 1.82 | 6.22 | 3.42x | 66 | yes |
| `201_2500_NR_24` | 1.72 | 5.32 | 3.10x | 66 | yes |
| `201_2500_NR_25` | 1.91 | 5.12 | 2.68x | 66 | yes |
| `201_2500_NR_26` | 2.81 | 5.22 | 1.86x | 66 | yes |
| `201_2500_NR_27` | 1.71 | 5.52 | 3.22x | 66 | yes |
| `201_2500_NR_28` | 1.81 | 4.82 | 2.66x | 66 | yes |
| `201_2500_NR_29` | 1.82 | 5.62 | 3.09x | 66 | yes |
| `201_2500_NR_30` | 1.81 | 6.12 | 3.38x | 66 | yes |
| `201_2500_NR_31` | 1.92 | 4.71 | 2.46x | 66 | yes |
| `201_2500_NR_32` | 2.11 | 5.82 | 2.76x | 66 | yes |
| `201_2500_NR_33` | 24.45 | 5.12 | 0.21x | 66 | yes |
| `201_2500_NR_34` | 12.83 | 6.61 | 0.52x | 66 | yes |
| `201_2500_NR_35` | 2.01 | 5.32 | 2.64x | 66 | yes |
| `201_2500_NR_36` | 1.91 | 5.92 | 3.10x | 66 | yes |
| `201_2500_NR_37` | 1.81 | 6.02 | 3.32x | 66 | yes |
| `201_2500_NR_38` | 13.43 | 5.92 | 0.44x | 66 | yes |
| `201_2500_NR_39` | 17.73 | 5.92 | 0.33x | 66 | yes |
| `201_2500_NR_40` | 1.92 | 5.62 | 2.93x | 66 | yes |
| `201_2500_NR_41` | 2.22 | 6.02 | 2.71x | 66 | yes |
| `201_2500_NR_42` | 1.91 | 5.82 | 3.04x | 66 | yes |
| `201_2500_NR_43` | 1.81 | 7.42 | 4.10x | 66 | yes |
| `201_2500_NR_44` | 2.02 | 5.12 | 2.54x | 66 | yes |
| `201_2500_NR_45` | 2.32 | 6.12 | 2.64x | 66 | yes |
| `201_2500_NR_46` | 1.81 | 5.42 | 2.99x | 66 | yes |
| `201_2500_NR_47` | 1.82 | 5.51 | 3.04x | 66 | yes |
| `201_2500_NR_48` | 1.92 | 4.92 | 2.57x | 66 | yes |
| `201_2500_NR_49` | 2.42 | 5.42 | 2.24x | 66 | yes |

## What's still open

- **ANI-402** (the 400-item family): not yet re-measured against this
  same default-flags baseline — see `docs/ROADMAP.md`.
- **Mean-case gap (~2.5x on the easier majority)**: root cause not
  further decomposed in this checkpoint; the per-node/per-iteration cost
  breakdowns in earlier `docs/STATUS.md` checkpoints (e.g. the
  `build_branch_groups` caching idea) remain the concrete next lead if
  closing this further matters more than the current absolute times.
- **Branching path** (`--branch-price`, non-root nodes): SAFE_MIP_SOL is
  root-only (matching the paper/legacy's own gate); this comparison only
  exercises the root, which is now sufficient for all 50 ANI-201
  instances, but the branching path's own performance is unaffected by
  this checkpoint.
