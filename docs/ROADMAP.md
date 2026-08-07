# Roadmap

This lists what is intentionally **out of scope** for the first release
(`v0.1.0`) and what is planned **after** it, so the classical-BPP core stays
small and does not get contaminated by unrelated variants
(`PIANO_REFACTORING_BPP.md`, "Obiettivo"). For the day-to-day state of
in-progress work, see `docs/STATUS.md` and `docs/CONTINUATION_STATE.md` —
this file only changes when the roadmap itself changes, not every session.

## In scope for v0.1.0

- Classical one-dimensional Bin-Packing Problem (integer capacity/weights,
  no conflicts, setups, classes, or profits).
- The paper's two-phase column generation (CPLEX and/or Gurobi floating
  phase, selectable via `LpBackend`/`--solver`; SoPlex/GMP rational safe
  phase), SR3 separation, Ryan-Foster branch-and-price, historical diving,
  opt-in historical dual-value stabilization (off by default -- confirmed
  unused in producing the paper's own results).
- Two node-exploration strategies (`NodeStrategy::BestBound`,
  `NodeStrategy::DepthFirst`), both exact.

## Explicitly excluded, not planned

- BPPS bilevel/strong branching, BPPC (conflicts/setup/classes), Formulation
  A/B, and the ancillary local-search program: these are historical
  archive-only variants outside the classical BPP target and will not be
  imported into this codebase (`docs/legacy-feature-inventory.md`).

## Planned after v0.1.0, roughly in priority order

1. **The paper's actual Algorithm 1** (Baldacci, Coniglio, Cordeau, Furini,
   INFORMS JOC 2023, Section 3, pp. 6-11) is **implemented for the root/SR3
   case** (`docs/STATUS.md`, 2026-08-07 "Algorithm 1 implemented for the
   root/SR3 case" checkpoint -- read the "numbers retracted" checkpoint
   right before it first for the derivation this is based on, and the
   further-back retracted greedy-seeding checkpoint should not be revisited
   in isolation, see that entry for why).

   In short: the paper's Algorithm 1 is **one loop**, not two disconnected
   phases. Every iteration prices under the *scaled-integer* duals
   `(p_int, r_int)` (never raw floating duals), which yields an exact
   diminished reduced cost `c_min` and a proven-valid lower bound
   `LBF = z_dim * K/(K - rc_int)` (Proposition 3) for free, every
   iteration -- so the rational solver is only engaged once the best
   reduced cost drops within the floating solver's own tolerance of zero,
   as a narrow continuation of the *same* loop (never switching back), not
   a full second pass rebuilt from scratch. `run_algorithm1_loop`
   (`src/master/column_generation.cpp`, templated over `MasterRmp`/
   `SoplexRmp`) implements this, backed by a new
   `price_scaled_integer_with_sr3` pricer (reuses the existing
   label-setting DP verbatim, fed exact-integer-valued `double`s) and
   `SafeBound::fathoming_bound` (exact GMP rational `LBF`). Wired in for
   `options.branching.constraints().empty()` (root calls, including a
   branch-and-price tree's own root node) via
   `solve_root_column_generation_algorithm1`.

   **Measured** (`docs/STATUS.md`, same checkpoint, full table): SoPlex is
   now not entered at all on 3 of 4 ANI-201 sample instances and on the
   ANI-402 sample, matching what direct `legacy/` instrumentation showed --
   `201_2500_NR_0.txt --root-cg` 10.5s->7.3s, `402_10000_NR_0.txt` 162.7s
   ->89.4s **and now certified** (previously needed the full tree),
   `201_2500_NR_0.txt --branch-price 50` 16.5s->7.3s. One sample instance
   (`201_2500_NR_11.txt`) drops below the default 4-round SR3 budget's
   certification threshold (`ceil(LB)=65 < UB=66`, a valid but weaker
   bound, not an unsoundness bug) -- recovers with the pre-existing
   `--sr3-max-rounds` flag, same known budget-sensitivity as before this
   change, not new. Full `ctest` passes on all four build configurations.

   **Update, same day: extended to branch-and-price nodes too**
   (`docs/STATUS.md`, "Algorithm 1 extended to branch-and-price nodes"
   checkpoint). `price_scaled_integer_with_branching_and_sr3` (same
   exact-integer-`double` reuse trick) plus a `bound_margin` fix on the
   branch-aware DP's own fractional-bound pruning; `solve_two_phase_root_
   column_generation` now dispatches to Algorithm 1 unconditionally, not
   just for empty branching. Real but **modest** win (`201_2500_NR_11.txt
   --branch-price`: 42.8s->36.0s, ~16%; full 50-instance sweep branch-price
   mean ratio 16.15x->14.15x), much smaller than the root case's ~30-45%.

   **Root-caused why**: per-node timing showed each branching node costs
   9-14s despite phase II (SoPlex) also being skipped there (confirming
   the mechanism works) -- the extra cost is `build_branch_groups`
   (Together-contraction/Different-conflict-adjacency) being rebuilt from
   scratch on *every* pricing call within a node (hundreds of times),
   even though only the *dual values* it assigns change between
   iterations, not the node's branching *structure*. **Not fixed yet**:
   splitting that function into a cacheable structural part and a cheap
   per-iteration value part is the precise, scoped next optimization.

   **Resolved, same day**: the 6/50 stuck instances above were not a
   branching problem at all (`docs/STATUS.md`, "SAFE_MIP_SOL implemented"
   checkpoint). Running the official binary on `NR_13` directly showed
   `Nodes 0` -- it never branches for this instance, it uses `SAFE_MIP_SOL`
   (`DP_POP.cpp:910-978`): enumerate patterns, then solve a genuine 0/1
   covering MIP with a "beat the incumbent" row -- infeasible proves the
   incumbent optimal outright, no branching or LP-bound-reaching-the-
   ceiling required. Implemented (`CplexRmp::solve_mip_at_most`,
   `try_safe_mip_certification`, opt-in via `--populate`) -- and while
   implementing it, found that `populate_root_columns`'s own enumeration
   was a plain DFS with no dominance pruning (unlike legacy's
   `DP_LABEL_SETTING_POPULATE`, which reuses the same pricing DP engine),
   measured hanging past 90s on `NR_11`. Rewrote it onto the existing
   dominance-pruned `price_label_setting_with_sr3` (same engine, threshold
   substituted for bin-cost) instead of patching the DFS. Result: all 6
   previously-stuck instances now certify in 5.6-9.7s each, and the full
   50-instance ANI-201 sweep is **50/50 certified** (0 stuck), mean ratio
   to the official binary **2.65x** (was ~7-8x at the start of this
   session). Not yet done: Gurobi MIP support, wiring this into the
   branching/tree case, and re-measuring ANI-402 and the
   `build_branch_groups` caching idea against this new baseline.

   Also open: preserving the round-persistent master across SR3 rounds in
   the new driver (rebuilds a fresh `MasterRmp` per round instead -- not
   the measured bottleneck, so not prioritized), and Fathoming Rule 1's
   benefit for actual branch-and-price nodes with a tight incumbent (only
   exercised at the root so far, where it rarely fires early since the
   incumbent there is just the greedy heuristic).

   Two shortcuts were considered and explicitly rejected as not faithful to
   the paper before landing on the implementation above (`docs/STATUS.md`,
   "numbers retracted" checkpoint): forcing SoPlex's `SOLVEMODE_AUTO`
   (measured slower, changes nothing since zero tolerances force full
   rational refinement anyway) and pruning the pool handed to SoPlex to
   only positive-primal-value columns (not something legacy or the paper
   does).

   **Retracted** (kept for history, do not revisit): an earlier checkpoint
   claimed legacy's root starts already-near-optimal (`current_LP=65.03`,
   927 columns at iteration 1) due to greedy-incumbent pool seeding, and
   that a faithful port of that seeding was implemented and measured
   net-negative. Re-instrumenting the same legacy binary fresh did not
   reproduce those numbers -- the real root takes 393 iterations, 2.6s wall
   time, converging gradually like this codebase's own root does. The
   seeding revert itself was still the right call, just for the wrong
   diagnosed reason.
2. Sub-quadratic-in-spirit dominance for the SR3-aware pricing DP is now
   **fixed and re-enabled** (`docs/STATUS.md`, 2026-08-07 "SR3 dominance
   correctness fix" checkpoint): a real unsoundness bug in the componentwise
   dominance rule (unsound for negative-dual cuts) was found and fixed
   before re-enabling it, bounded to O(n*window) via a sorted sliding
   window (mirroring legacy's own `PARAM_DELTA`-bounded comparison) instead
   of the two previously-abandoned O(n^2) attempts. New `--sr3-max-rounds`
   flag now makes 10 simultaneous SR3 cuts practical (~20s on
   `201_2500_NR_0.txt`, matching the official binary's own cut count on
   that instance), where before this fix more than 4 was not viable at all.
   Still open: pushing past ~10-12 simultaneous cuts remains expensive
   (15 rounds exceeded 60s even with the fix) -- a genuinely sub-quadratic
   structure (bucket-by-load skyline) rather than a bounded-window
   heuristic pass would be needed to go further, if still worth it after
   item 1 above is addressed.
3. A master/row-level persistent warm start for `NodeStrategy::DepthFirst`
   (CPLEX bound-toggling across backtracking instead of the current
   pool-level warm start) to close the remaining gap to the historical
   executable's per-node timing on deep trees -- now unblocked by the
   pricer fix above, but still a real correctness risk (bound
   enable/disable/restore across recursive backtracking) that needs careful,
   isolated implementation.
4. LP-heuristic root/non-root scheduling distinction, matching
   `BPPS_BP_LP_HEUR.cpp` (diving and dual-value stabilization are already
   ported, both opt-in; this is the remaining historical primal/dual
   heuristic scheduling gap -- the heuristic itself already matches
   legacy's per-iteration frequency, only the root-vs-non-root trigger
   distinction is missing).
5. The complete ANI-201/ANI-402 regression sweep (UB/LB, nodes, triplets,
   columns, iterations, time) against the historical executable, including
   real branching cases now that the pricer fix makes deep trees practical
   to measure, beyond the samples already measured in `docs/STATUS.md`.
6. `docs/algorithm.md` and `docs/numerical-safety.md`, linking each module to
   the corresponding section/proposition of the paper.

`LICENSE` (MIT, all four paper co-authors) was confirmed on 2026-08-07; the
`legacy/` historical reference implementation stays local-only, never
published (`.gitignore`'s `/legacy/` rule, `NOTICE`). Gurobi was restored the
same day as an optional, alternative floating-point LP backend to CPLEX
(`include/bpp/gurobi_rmp.hpp`, `-DBPP_ENABLE_GUROBI=ON`), reversing the
earlier "removed by design" decision -- see `docs/STATUS.md`'s "Gurobi
backend restored" checkpoint. Either CPLEX or Gurobi alone is a complete,
supported build; the CPLEX-vs-Gurobi equivalence test only runs when both
are built in.

## Ideas not yet scheduled

- A benchmark-format reader beyond the current minimal `<n> <capacity>` plus
  weights format, if a community-standard BPP format is requested.
- Packaging (e.g. a Conan/vcpkg recipe) once the CPLEX/Gurobi-mandatory
  constraint has a documented workaround for users who only want the
  portable build.
