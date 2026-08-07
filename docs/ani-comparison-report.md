# ANI 200/400 comparison report

## Scope

The benchmark is the classical BPP path with the historical populate phase
disabled (`PARAM_ENUMERATION=0`). The available ANI families are ANI201 and
ANI402, corresponding to the requested 200/400-item families under the
historical sentinel convention.

For every instance the comparison records status, incumbent/upper bound, the
raw restricted-master LP value, certified global LP lower bound (when available), nodes,
generated columns, exact pricing calls, triplets and time, then compares the
new solver's status and bin count.

## Baseline currently executed

The historical executable was built with the local CPLEX/SoPlex installation
and run with the six legacy arguments. The two completed reference rows are
copied from `docs/legacy-info/ani-baseline-info_EXTRA.txt` and
`docs/legacy-info/ani-baseline-info_Exensive.txt` and
normalized in `tests/results/ani-baseline.csv`:

| family | instance | old UB | old LP bound | nodes | columns | exact DP calls | triplets | time (s) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ANI201 | `201_2500_NR_0.txt` | 66 | 65.000004618703 | 0 | 3,725 | 390 | 10 | 2.188517 |
| ANI402 | `402_10000_NR_0.txt` | 133 | 132.000000024113 | 18 | 9,472 | 988 | 50 | 30.072316 |

Both historical runs returned status 101. No new-vs-old equivalence claim is
made until the exact new path has been run.

Status 101 is the legacy solver's termination code, not a portable exactness
certificate. In particular, its finite-precision `BP_lp` can be slightly above
an integer and lead to root termination; the refactored safe rational bound
rounds conservatively and may require Ryan--Foster branching for the same
instance.

The local append-only logs also contain a different two-row experiment using
`param_test_BPP_ANI_si_SOPLEX_si_POP.txt`: ANI201 has the same UB 66 and 3,725
columns, while ANI402 uses 9,996 columns and takes 61.84 seconds. Those rows
are SoPlex plus enumeration/populate and are therefore not interchangeable
with the no-populate baseline above; they are tracked in
`docs/legacy-parameter-matrix.md`.

The compatibility configuration is the historical no-SoPlex/no-population
variant: label-setting batch size 10, safe scale `1e13`, and one floating
root-CG phase. ANI201 `--legacy-root-cg 1000` closes at LP 65, incumbent 66;
the run takes 377 iterations and retains 3,690 columns. The explicit
`--root-cg 1000` diagnostic then adds one safe iteration
(`phase1_iterations=377`, `phase2_iterations=1`) and certifies the same bound;
with `BPP_ENABLE_SOPLEX=ON` it reports `phase2_backend=soplex`. The old
reference uses 3,725 columns and 390 exact DP calls; the remaining gap is now
small and measurable.

With `BPP_ENABLE_SOPLEX=ON`, the second phase reports `phase2_backend soplex` and
reaches the same toy/root safe-certificate values as the GMP fallback.

For ANI402 with `--root-cg 1000`, the new path reaches LP 132 and incumbent
133 after 865 floating iterations, 8,795 columns and 64.06 seconds. It remains
`iteration_limit` because the safe phase was not reached; the old no-populate
reference is LP 132.000000024113/UB 133 after 18 tree nodes and 9,472 columns.

A root pricing certificate is not an integer-optimality certificate when
`ceil(LB) < UB`: in the ANI201 root rows the certificate is `LB=65` and the
incumbent is `66`. The complete Ryan--Foster tree is required to close this
one-bin gap.

`tests/results/ani-comparison.csv` now contains all 100 ANI201/ANI402 files
(50 per family) from a no-populate bounded sweep (`NEW_MAX_ITERATIONS=20`,
two-second process limit) in `NEW_MODE=legacy-root-cg`. It includes phase
counters and backend fields; phase 2 is correctly zero in this compatibility
mode. All 100 rows reproduce the historical incumbent bin count
(`ub_equal=1`). Since all 100 bounded runs are `iteration_limit`, the new
`new_lb` field is correctly `NA` in all rows; `new_lp_relaxation` is retained
only as a diagnostic and must not be interpreted as a valid global LB. The two
complete historical reference rows above remain authoritative until a long run
(`TIME_LIMIT=3600`) is completed.

The comparison against the paper's published tables, including the distinction
between the complete BCCF workflow and this isolated root baseline, is in
`docs/paper-comparison.md`.

## Executed old/new comparison results

The following results are measured outputs, not expected values:

| test | old reference | new result | interpretation |
| --- | --- | --- | --- |
| ANI201, ten-instance no-populate root sample | UB baseline; mean total 5.505 s | 10/10 UB equal, 10/10 valid global LP certificates; mean wall 4.765 s | root behavior agrees on UB/LB-LP, but not an integer-optimality proof when `LB=65, UB=66` |
| ANI201, same ten instances with SoPlex two-phase root | mean old diagnostic time 5.419 s | 10/10 UB equal, 10/10 valid global LP certificates; 388.4 phase-I and 36.2 phase-II iterations; mean 10.193 s | backend comparison only; full tree/triplet schedule not active |
| ANI201 + ANI402, bounded 100-row sweep | all historical incumbent values available | 100/100 `ub_equal=1`, 100/100 `iteration_limit`, 0/100 valid `new_lb` | UB smoke test only; restricted LP values are deliberately not reported as LBs |

The raw files are `tests/results/ani201-10-comparison.csv`,
`tests/results/ani201-10-root-soplex-comparison.csv` and
`tests/results/ani-comparison.csv`. The old `old_lp_bound` field is the legacy
floating LP diagnostic; only `new_lb` with `new_lb_valid=1` is a safe global
bound. These tests therefore document the current behavior without claiming
that the new root driver has already reproduced the paper's exact integer
tree results.

For ANI201 `201_2500_NR_0.txt`, for example, the safe bound is
`649999999999899/10000000000000` (approximately 65) and the incumbent is 66;
therefore `ceil(LB)=65` and the root cannot certify integer optimality. The
Ryan--Foster tree is required to close this one-bin gap.

The production root command `--root-cg` reports `automatic_sr3=1` and
performs bounded SR3 separation/restarts at the root. The compatibility
commands `--no-populate` and `--legacy-root-cg` report `automatic_sr3=0` by
design, matching the historical no-populate baseline.

The post-refactoring runner was also checked with the local exact executable
on five ANI-201 instances (`tests/results/ani-5-latest.csv`), using a
200-iteration cap. All five incumbents matched the legacy UB; all five rows
were correctly labelled `iteration_limit`, with `new_lb_valid=0` and no
uncertified LP value promoted to a global LB. The CSV now records SR3 and
populate counters for every run.

## Reproduction

The portable target is available at `build/bpp-solve`; for the complete exact
backend use `build-cplex-soplex/bpp-solve` after configuring CPLEX+SoPlex as
described in the README. To rerun the sweep, use:

```sh
TIME_LIMIT=3600 NEW_MAX_ITERATIONS=10000 scripts/run_ani_comparison.sh
```

The script discovers all ANI201 and ANI402 files, runs old and new commands
without populate (the compatibility default uses `--legacy-root-cg`; set
`NEW_MODE=root-cg` to exercise the safe phase), keeps per-instance logs
under `tests/results/ani-logs/`, and writes `tests/results/ani-comparison.csv`.
The `ub_equal` column is `1` only when the incumbent values are identical.
For a bounded smoke sweep, lower `TIME_LIMIT` and `NEW_MAX_ITERATIONS`; timed
out rows are retained with their process return code.

## Implementation status

The refactored tree now has the validated model, preprocessing, CPLEX RMP,
root floating column generation, rational SoPlex/GMP safe phase,
Ryan--Foster-aware pricing for Together/Different branches, SR3 separation,
per-load label dominance/fathoming, stabilization controls and LP-diving/
rounding heuristics. The command remains explicitly marked `iteration_limit`
when the configured CG budget expires. SR3 rows and their dual-aware pricing
oracle are available through the CPLEX RMP API, with SR3-aware safe-bound
certification; automatic triplet separation is now called by `--root-cg` and
`--populate` through bounded root restarts, while compatibility mode remains
cut-free. Strict old-vs-new equivalence still requires the long all-instance
ANI sweep, historical activation/counters and the complete tree, so this
document does not claim it from bounded measurements alone.
