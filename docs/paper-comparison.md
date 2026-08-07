# Comparison with the paper tables

The paper's Table 1 reports the complete BCCF algorithm (SR3 separation,
SoPlex phase, pattern enumeration and the full Ryan--Foster tree), on 50
instances per group. For ANI-201 it reports 50/50 exact solutions and 13.6 s
average time. Table 2 reports 279.7 average phase-I columns and 1.6 s phase-I
time; Table 3 reports 56.20 phase-I iterations.

The reproducible sample in
`tests/results/ani201-10-comparison.csv` is deliberately labelled separately:
it runs the historical no-SoPlex/no-populate configuration with the new
floating root driver (`--legacy-root-cg`) on the first ten ANI201 files. The
current sample gives:

| metric (10 instances) | old executable | new root driver |
| --- | ---: | ---: |
| root converged (pricing certificate) | 10/10 | 10/10 |
| UB equality | — | 10/10 vs old |
| certified global LP LB | 10/10 historical rows | 10/10 |
| mean total/wall time (s) | 5.505 | 4.765 |
| mean columns | 3,927 | 3,814 |
| mean root iterations | — | 388.4 |

These numbers are not a claim of paper-level performance parity: the paper
benchmark includes the full tree and populate workflow, whereas this sample
isolates the no-populate root baseline. In particular, the new root iteration
count and column count are not comparable to Table 2/3 until automatic SR3
separation, historical SoPlex hand-off and pattern enumeration are run in the
same configuration.

The old executable's `status=101` rows can terminate at the root because its
finite-precision LP value is slightly above an integer (for example
`65.0000046`). The safe rational result deliberately rounds this value down to
`LB=65`; consequently the old total time and the new root time are diagnostic
only, not an apples-to-apples exact-proof timing comparison.

Also, a root pricing certificate is not an integer-optimality certificate when
`ceil(LB) < UB` (this occurs in the sample: `LB=65`, `UB=66`). The paper's
Table 1 counts an instance as exact only after the complete Ryan--Foster tree
has closed this gap. A tree run is therefore required before claiming the
paper's 50/50 exact-solution result.

The bounded all-instance CSV uses `new_lp_relaxation` only as a diagnostic.
`new_lb` is `NA` unless the run is converged and the safe-dual certificate is
valid; a restricted-master LP value from an `iteration_limit` run is not a
global lower bound and is never reported as one.

## SoPlex root recalculation

To separate backend effects from the compatibility run, the same ten ANI201
instances were rerun with `--root-cg` and the SoPlex build. The generated
`tests/results/ani201-10-root-soplex-comparison.csv` gives 10/10 root pricing
certificates, 10/10 equal UBs, and 10/10 valid global LP certificates:

| metric | paper Table 2/3 (ANI-201) | old no-pop executable | new ten-instance SoPlex root |
| --- | ---: | ---: | ---: |
| phase-I iterations | 56.20 | not exposed | 388.4 |
| phase-I columns | 279.7 | 3,926.9 | 3,849.2 total final columns |
| phase-II iterations | 4.42 | not used | 36.2 |
| mean time (s) | 13.6 complete BCCF | 5.419 diagnostic | 10.193 root only |

For reference, the paper's Table 3 ANI-201 timing split is phase I `0.23 s`
LP plus `0.49 s` DP per instance, and phase II `0.13 s` LP plus `0.01 s` DP.
The current CSV does not yet expose an equivalent LP/DP split, so those fields
are intentionally not inferred from wall time.

The differing columns/iterations confirm that the current root driver is not
yet numerically aligned with the paper's complete BCCF configuration. The
paper's averages include its exact historical pricing/heuristic schedule,
SR3 separation, populate enumeration and tree; these must be measured with a
like-for-like full-tree run before interpreting the time ratio.

At this checkpoint the refactored code contains the SR3/triplet row and pricing
implementations and the automatic root separation restart. The compatibility
sample is intentionally cut-free, while the new production root smoke run
generated one cut before its iteration limit. Historical activation frequency,
triplet counters and full-tree interaction still need validation before this
can be compared as the paper's full BCCF implementation.

## Post-refactor regression check (2026-08-06)

After adding the SR3 gap-activation/total-cut-budget gates and the tree
fathoming/optimality-certificate fixes (`docs/STATUS.md` refactoring
checkpoints), the comparison script was re-run to confirm no regression:

- `--legacy-root-cg`, 10 ANI-201 instances, 20 iterations:
  `tests/results/ani201-10-post-refactor.csv` — 10/10 UB equality against the
  historical executable (all `66`), same iteration-limited/no-certified-LB
  shape as the pre-refactor `ani201-10-comparison.csv` baseline.
- `--root-cg` (SoPlex two-phase), 5 ANI-201 instances, 200 iterations:
  `tests/results/ani201-5-root-soplex-post-refactor.csv` — 5/5 UB equality;
  all five hit the 200-iteration cap before the floating phase converged (so
  the new SR3 gate never got a chance to separate — 0 cuts — which matches
  the CG-convergence precondition already documented above, not a gate
  regression).

These runs confirm the phase-1/phase-2 changes preserve UB correctness on a
live sample; they are not a substitute for the full ANI-201/ANI-402 sweep
still required before v0.1.0 (`docs/CONTINUATION_STATE.md` item 4).

## Post-refactor performance check (2026-08-07 night/morning)

Everything above predates the two fixes that closed most of the performance
gap: the sparse label-setting DP for the pricing DP (`docs/STATUS.md`,
"ANI-402 fixed" checkpoint) and forcing `CPX_PARAM_LPMETHOD=CPX_ALG_PRIMAL`/
`CPX_PARAM_THREADS=1` on the RMP to match the ANI parameter files
(`docs/STATUS.md`, "CPLEX threading fix" checkpoint). The numbers above are
therefore obsolete for judging current performance; use this section and
`docs/STATUS.md` instead.

**Scope caveat, stated plainly**: all figures below are **root-only**
(`--root-cg`, CPLEX+SoPlex two-phase, automatic SR3 separation), on small
samples (15/50 ANI-201, 1 ANI-402 instance measured individually), not the
paper's full 50-instance Ryan-Foster tree run. They show the root driver is
now close to the paper's *complete*-algorithm time on ANI-201 instances that
happen to close at the root — most of the 15-instance ANI-201 sample does.
They are **not** yet a reproduction of the paper's Table 1/2/3 exact-solution
count, because (a) the sample is a fraction of the paper's 50 instances per
family, and (b) any instance that needs real Ryan-Foster branching to close
the gap (`ceil(LB) < UB` at the root) currently runs into the newly-found
branching-node pricer bottleneck (`REPORT_REFACTORING_BPP.tex`, "Nuova
scoperta più importante"), so a full-tree paper-equivalent number cannot be
claimed yet for either family.

| metric | paper Table 1 (ANI-201, 50 instances, complete BCCF) | this codebase (15 instances, root only, `tests/results/ani201-15-final-perf.csv`) |
| --- | ---: | ---: |
| exact/converged | 50/50 | 15/15 root pricing certificates (not all integer-optimality-certified without a tree pass) |
| mean time (s) | 13.6 | **11.77** |
| mean columns | 279.7 (phase-I) | 3,976 total (phase-I+phase-II, different accounting — not directly comparable) |
| SR3 cuts/instance | up to `count_triplets` per the parameter file (validated at 10 for `201_2500_NR_0.txt`, `REPORT_REFACTORING_BPP.tex`) | 4 (current `max_sr3_separation_rounds` cap, deliberately conservative — see report) |

| metric | legacy executable (`402_10000_NR_0.txt`, SR3-free) | this codebase (SR3-free) | this codebase (with automatic SR3) |
| --- | ---: | ---: | ---: |
| time (s) | 31.9 | 48.5 | 222.7 (not yet integer-optimality-certified: `ceil(LB)=132 < UB=133`, needs the tree) |

The batch file `tests/results/ani402-5-final-perf.csv` runs 5 ANI-402
instances with automatic SR3 under a 120s cap and all 5 report `status=124`
(cap hit) — consistent with the single-instance 222.7s figure above, which
exceeds that cap; it is not a new regression, just a tighter time limit than
the individual measurement used.

**Where to look for the underlying data**: `docs/STATUS.md` (checkpoint
"2026-08-07 night: CPLEX threading fix...") for the narrative and
before/after numbers; `tests/results/ani201-15-final-perf.csv` and
`tests/results/ani402-5-final-perf.csv` for the raw per-instance rows;
`REPORT_REFACTORING_BPP.tex`/`.pdf` for the full writeup including the
branching-pricer finding that blocks a genuine full-tree paper-table
reproduction. A true paper-equivalent table (50/50 instances per family,
full tree, populate) is still open work (`docs/ROADMAP.md`, item 5).

## Direct comparison against the official BCCF reference binary (2026-08-07)

Every comparison above this point was against the `legacy/` archive --
Fabio Furini's own historical development copy of the algorithm, not the
paper's own published distribution. `../official-reference-BCCF/` (a
sibling directory to this repository, not part of it, never published --
see its `README.md`) downloads and builds the actual code the paper's
authors distribute at
<https://github.com/stefanoconiglio/A-Numerically-Exact-Algorithm-for-the-Bin-Packing-Problem>,
linked locally against CPLEX 20.1/Gurobi 13.0.2/SoPlex 5.0.1 (the official
README specifies CPLEX 12.9/Gurobi 9.5.1/SoPlex 5.0.1 "in order to obtain
results comparable to those of the paper"; linking against the newer
CPLEX/Gurobi versions installed on this machine succeeded and ran
correctly, but is not guaranteed to reproduce the paper's numbers bit-for-
bit -- treat this as a "right ballpark" check, not a certified
reproduction).

Run with the paper's own best-performing parameter file
(`param_test_BPP-non-IRUP-2exp44.txt`) against `bpp-solve --root-cg`
(automatic SR3, default settings) on the same real instances:

| Instance | Official BCCF | `bpp-solve --root-cg` | UB match | Official `count_triplets` | Ours `sr3_cuts_added` |
| --- | ---: | ---: | :---: | ---: | ---: |
| `201_2500_NR_0.txt` | 1.52s | 12.3s | 66 = 66 | 10 | 4 |
| `201_2500_NR_1.txt` | 1.40s | 17.0s | 66 = 66 | 10 | 4 |
| `201_2500_NR_10.txt` | 1.62s | 11.1s | 66 = 66 | 20 | 4 |
| `201_2500_NR_11.txt` | 1.41s | 10.2s | 66 = 66 | 10 | 4 |
| `402_10000_NR_0.txt` | 27.2s (root 15.4s) | 222.7s (`--root-cg`, prior measurement) | 133 = 133 | 50 (capped) | 4 |

**Correctness**: every instance reaches the identical certified integer
optimum -- this is the first correctness cross-check against the actual
published reference rather than the personal `legacy/` archive, and it
holds. **Performance**: the official binary is roughly 7-19x faster on
ANI-201 and roughly 8x faster on ANI-402 (root only), *while separating
more SR3 cuts, not fewer* (10-50 vs. our capped 4). This confirms -- against
the real reference, not just `legacy/` -- that the SR3-aware pricing DP's
missing cross-state dominance (`docs/STATUS.md`, `docs/ROADMAP.md` item 1)
is the dominant remaining performance gap, and that it is a genuine,
roughly order-of-magnitude gap rather than a measurement artifact of
comparing against a possibly-unrepresentative personal archive build.
