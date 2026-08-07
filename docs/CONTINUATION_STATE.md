# Continuation state for the BPP refactoring

This file is the handoff for a new LLM/agent continuing the work. Read it
before changing code.

## Workspace and scope

- Project: `/home/fabio/Dropbox/AA_WORKING/BPP_BP_CODE/BPP_ITALIA/bpp-numerically-exact`
- Scope: classical one-dimensional BPP only.
- The target behavior is the historical BPP code and the base BCCF algorithm
  in Baldacci et al. (2023). Do not add unrelated variants or new features.
- User requirements: preserve historical pricing, branching, diving, primal
  heuristics, stabilization, safe bounds, populate and tree semantics.
- Git: `.git` exists (initialized 2026-08-07). The first commit was made
  2026-08-07 after explicit user go-ahead (`legacy/` and files with
  embedded personal absolute paths excluded, see `.gitignore`). Still not
  pushed to any remote -- do not push without a separate, explicit
  go-ahead; a commit is not a push authorization.

## Top priority, resolved (2026-08-07: Algorithm 1 implemented; all 50 ANI-201 instances certify)

**Read `docs/STATUS.md`'s last three 2026-08-07 checkpoints in full before
touching `src/master/column_generation.cpp` or
`src/pricing/floating_root_pricer.cpp`** -- they are the most consequential
changes in the project's history so far and supersede most of the
"Known incomplete work" item 1 entry below, which predates them.

In short: this codebase's column generation used to run two disconnected
phases (floating duals, then an unconditional full second pass in
rational SoPlex). The paper's actual Algorithm 1 (Baldacci et al. 2023,
Sec. 3.3) is one loop that prices under *scaled-integer* duals every
iteration and only narrowly engages the rational solver -- implemented in
`run_algorithm1_loop`/`solve_root_column_generation_algorithm1`, for both
root and branch-and-price nodes. Separately, the paper's Sec. 4 "reduced
SC problem" mechanism (legacy: `SAFE_MIP_SOL`) -- populate the pattern
pool, then solve a genuine 0/1 covering MIP, where infeasibility is itself
a complete optimality proof independent of the LP bound -- is implemented
as `CplexRmp::solve_mip_at_most`/`try_safe_mip_certification`, opt-in via
`--populate`. While implementing it, `populate_root_columns`'s enumeration
(a hand-rolled DFS with no dominance pruning) was found hanging past 90s
on a real instance and was rewritten onto the same dominance-pruned
pricing DP already used for column generation.

Net result, measured against the official BCCF binary on all 50 ANI-201
instances (not a sample): 50/50 certify (was 44/50, with 6 instances not
finishing within 90s of branch-and-price before this work), mean time
ratio to the official binary 2.65x (was ~7-8x before this session's
Algorithm-1 investigation started). `ctest` passes on all four build
configurations.

**Not done, real next steps**: Gurobi MIP support (`GurobiRmp` has no
`solve_mip_at_most`); wiring `try_safe_mip_certification` into non-root
branch-and-price nodes (currently root-only, matching legacy's own
`level==0` gate); re-measuring ANI-402 and the `build_branch_groups`
per-iteration caching idea (identified, not implemented -- see
`docs/STATUS.md`'s "Algorithm 1 extended to branch-and-price nodes"
checkpoint) against this new baseline.

## Current implementation

Implemented and tested in the refactored C++ project:

- validated BPP model, parser, solution validator and preprocessing;
- CPLEX and/or Gurobi restricted master (`MasterRmp`, `LpBackend`, see
  "Important files" below) and floating root column generation;
- label-setting pricing with persistent labels, batch pricing, dominance and
  suffix/fathoming bounds;
- deterministic rounding/LP-diving primal incumbent heuristics;
- GMP safe-bound arithmetic and fixed-point safe pricing;
- rational SoPlex RMP safe phase when `BPP_ENABLE_SOPLEX=ON`;
- Ryan--Foster Together/Different branch-aware pricing and a best-bound tree
  driver; BPPS-only bilevel branching is intentionally excluded;
- SR3/triplet row representation, bounded automatic root separation,
  SR3-aware pricing and safe-bound certification;
- automatic SR3 separation is also enabled for production branch-and-price
  nodes; compatibility `--legacy-root-cg`/`--no-populate` remains cut-free;
- bounded root pattern population through `--populate MAX_COLUMNS`;
- post-population rational SoPlex re-certification of the enlarged master;
- comparison runner and corrected LB semantics in
  `scripts/run_ani_comparison.sh`;
- (2026-08-06) SR3 gap-based activation (`sr3_gap_activation`) and cumulative
  cut-budget (`max_sr3_cuts_total`) gates, mirroring legacy
  `PARAM_TRIPLET_GAP_ACT`/`PARAM_MAX_TRIPLETS`;
- (2026-08-06) branching pair tie-break by combined weight
  (`BPPS_BP_TREE.cpp:314`), corrected `BranchAndPriceResult::optimal`, and a
  certified tree-wide `lower_bound` exposed by the CLI;
- (2026-08-06) `legacy/include/cplex.h`/`cpxconst.h`/`gurobi_c.h` are now
  gitignored (see `legacy/include/README.md`) so they cannot be committed.
- (2026-08-07 night) CPLEX RMP now forces primal simplex + single thread
  (`CplexRmp` constructor), matching `PARAM_SIMPLEX=1`/`PARAM_CPU=1` in the
  ANI parameter files (`BPPS_BP_MASTER.cpp:1382-1383`) — the single biggest
  remaining performance fix: ANI-402 baseline 87s->48.5s (legacy: 31.9s),
  ANI-201 mean 27.2s->11.8s (paper: 13.6s for the complete algorithm). Also:
  `dive_master_solution` was O(bins x patterns) (repeated full-pool
  rescans), rewritten to the single-sorted-pass shape legacy's
  `load_sol_1/2/3` actually use; DP frontier buffers reused instead of
  reallocated per item (minor). SR3 cross-state dominance was tried and
  found net-negative (see docs/STATUS.md) and reverted — do not re-add it
  without a sub-quadratic (not O(n^2)) dominance structure.
- (2026-08-07 night) Diving ported: `DivingDriver`
  (`src/search/branch_and_price.cpp`) faithfully reproduces
  `BPPS_BP_DIVING.cpp`'s once-per-solve, Together-first, IJOC-selection
  (`select_most_confirmed_pair`, `src/search/branching.cpp`), bounded-detour
  (`PARAM_TOKEN_DIV`) dive. Opt-in via
  `BranchAndPriceOptions::diving_enabled` / CLI `--diving`.
- (2026-08-06 night) SR3 performance fix: persistent master with incremental
  `CplexRmp::add_cut`, and a label-setting DP SR3 pricer
  (`price_label_setting_with_sr3`) replacing the exponential DFS. Validated
  on 15 real ANI-201 instances: 15/15 converge, UB-correct, certified LB,
  mean 27.2s (paper: 13.6s mean for the complete root+tree algorithm).
- (2026-08-07) `price_label_setting`/`price_label_setting_with_sr3` rewritten
  from a dense capacity-indexed array to a sparse, dominance-pruned label
  frontier with a tight fractional-knapsack fathoming bound, matching
  legacy's real label-setting DP; fixes ANI-402 (402 items, capacity 7552)
  never converging even without SR3.
- (2026-08-06 late night) `NodeStrategy::DepthFirst` (historical
  Together-first traversal order with a cross-tree warm-start pattern pool)
  alongside the existing `NodeStrategy::BestBound`, selectable via
  `--branch-price N --strategy depth-first`; CLI rewritten to accept named
  flags (`--strategy`, `--sr3-gap-activation`, `--sr3-max-cuts`) in addition
  to the legacy positional argument; `README.md`'s stale build path fixed;
  5 stray `info_*.txt` run-artifacts removed from the project root and
  gitignored.

Important semantic rule: `lp_bound` is only the current/restricted-master LP
diagnostic. `new_lb` is valid only when the pricing run converged and the safe
dual certificate is feasible. An iteration-limited restricted LP is not a
global lower bound and must remain `NA`.

## Top priority, resolved (2026-08-07: branching-aware pricer fixed)

**The branching-aware pricer bottleneck flagged in the previous checkpoint
is fixed.** `run_floating_pricing_loop` and the two SoPlex/CPLEX phase-2
loops no longer route non-empty-branching nodes to the one-column-per-call
DFS (`price(instance,duals,branching)`/`price_with_branching_and_sr3`, kept
only as a correctness reference and an overflow fallback); they use the new
`price_label_setting_with_branching_and_sr3` (`src/pricing/floating_root_pricer.cpp`),
a sparse label-setting DP over Together-contracted elements with
Different-conflicts enforced by a packed per-label bitmask, batching
multiple columns per RMP resolve like the root DPs already did. Verified
against legacy source first (`BPPS_BP_MAPPING.cpp`, `DP.cpp:18-318`,
`mckpsc-ls.cpp:2451-2571`): legacy also carries no extra per-label branching
state, only the same item-set transformation (Together contraction,
Different conflict list) applied once per node before every pricing call.
Measured: `201_2500_NR_41.txt`, `--branch-price 5`, did not finish in 60s
before; now converges (tree fully closes, `integer_optimum_certified=1`) in
38.5s. Full detail, the cross-check tests, and the caps/fallback in
`docs/STATUS.md`'s 2026-08-07 "branching-node pricer fixed" checkpoint.

**Master/row-level persistent warm start is now unblocked but still not
attempted.** It was deliberately deferred while the pricer was still the
dominant bottleneck (a fast master wouldn't have mattered underneath a slow
pricer); now that nodes price quickly, it is worth reconsidering, but CPLEX
column bound-toggling with correct enable/disable/restore across recursive
backtracking remains a real correctness risk the user explicitly flagged
twice — approach it carefully, verifying branching state is reflected on
the master correctly at every level, not just on `BestBound`.

**`LICENSE` (MIT, naming all four paper co-authors) is confirmed.** Fabio
Furini confirmed on 2026-08-07 that the co-authors agree to publish this
repository under it. The companion decision — whether to also publish
`legacy/` (the historical reference implementation itself, not just its
vendor headers) — was resolved the same day: **no**, it stays local-only,
excluded wholesale via `.gitignore` (`/legacy/`), used only as a regression
oracle. `NOTICE` already documented this framing before the decision was
confirmed; no change needed there.

## Known incomplete work (priority order)

1. [Partially done, 2026-08-06] The two per-call historical SR3 activation
   gates are now implemented and tested: gap-based activation
   (`sr3_gap_activation`, legacy `PARAM_TRIPLET_GAP_ACT`) and a cumulative
   cut-budget cap (`max_sr3_cuts_total`, legacy `PARAM_MAX_TRIPLETS`), both in
   `include/bpp/column_generation.hpp` and `src/master/column_generation.cpp`.
   See `docs/STATUS.md` refactoring checkpoint for the exact legacy line
   references (`BPPS_BP_TRIPLETS.cpp`, `BPPS_BP_MASTER.cpp:4694-4753`) used to
   derive them. Still remaining: thread a single cumulative triplet counter
   and gap check across branch-and-price nodes (legacy state is tree-global,
   the new gates are scoped per `solve_root_column_generation` call), decide
   an analog for `PARAM_TRIPLET_OFF_FOR_DIVING`, and expose the new options as
   CLI flags. [2026-08-06 night] The "rebuilds each production node RMP after
   violated cuts" performance problem mentioned above is fixed, not
   remaining work: the master is now persistent across SR3 rounds
   (`CplexRmp::add_cut`, warm-started `solve()`), and the SR3-aware pricer
   was replaced from an exponential-ish DFS (`price_with_sr3`) with a
   label-setting DP carrying per-cut count state
   (`price_label_setting_with_sr3`), matching legacy's
   `DP.cpp:prepare_data_cuts_DP_LABEL_SETTING` approach of folding cut duals
   into the same efficient DP instead of degrading to a weaker algorithm.
   Real ANI-201 instances that previously never converged with SR3 enabled
   now converge in ~16s with a certified integer optimum, often already at
   the root. See `docs/STATUS.md` for full detail, including the DP's
   remaining ~3x-per-simultaneous-active-cut growth (exact-state
   memoization only, no cross-state dominance pruning yet) and why
   `max_sr3_separation_rounds`'s default dropped to 4. [2026-08-07 night]
   Exact componentwise cross-state dominance was implemented
   (`prune_dominated_sr3_labels`) and **measured net-negative**: only ~10%
   label reduction for ~0.1s/call cost, turning a 16.7s run into a 60s+
   timeout. [2026-08-07 morning] Tried a second variant (same sweep, only
   every 25th item instead of every item, to amortize the O(n^2) cost) --
   also net-negative (22.6s vs. 11.1s undominance-pruned on the same
   instance). Both reverted (threshold set to never trigger); the conclusion
   after two independent attempts is that no frequency of a flat O(n^2) scan
   helps, a genuinely sub-quadratic structure is needed. `--root-cg` on
   ANI-402 (4 SR3 cuts) now converges but takes 222.7s — practical, still
   slow; this growth ceiling is the direct cause. Also [2026-08-07 morning]:
   ran `bpp-solve-legacy` on `201_2500_NR_0.txt` and read its own
   `count_triplets 10` from stdout -- the refactored solver finds only 4 for
   the same instance, a direct, now-documented consequence of
   `max_sr3_separation_rounds=4` (chosen for speed) rather than a bug.
   Remaining work here: numerical schedule equivalence with the historical
   activation frequency/counters, and a real (sub-quadratic) fix for the
   growth ceiling that would let the round budget go back up toward
   legacy's cut counts without the 2026-08-06 regression returning.
   [2026-08-07, resolved] Found and fixed a real unsoundness bug in
   `cut_state_covers` before re-enabling it: the rule required a uniform
   "A's cut-count >= B's" regardless of dual sign, but for a negative-dual
   cut being *ahead* is not always favorable (completing it is a penalty) --
   a constructed counterexample confirmed a uniformly-`>=` label could be
   wrongly pruned. Fixed to be sign-aware per cut (see the function's own
   comment, `src/pricing/floating_root_pricer.cpp`), and re-enabled with a
   bounded sliding-window comparison (8 neighbors per entry, not the full
   O(n) suffix -- mirroring legacy's own `PARAM_DELTA`-bounded window,
   `mckpsc-ls.cpp`) instead of either of the two previously-abandoned O(n^2)
   sweeps. New `--sr3-max-rounds` CLI flag (previously only settable via
   `ColumnGenerationOptions::max_sr3_separation_rounds`, no CLI exposure):
   `--sr3-max-rounds 10` now converges `201_2500_NR_0.txt` in ~20s with 10
   simultaneous cuts, matching the real `count_triplets=10` legacy produces
   on that instance -- previously not practically reachable at all. Default
   stays at 4 (15 rounds still exceeds 60s even with the fix). Separately,
   profiled *why the official binary is faster even at equal cut count*
   (real timing data, not guesses): phase 2 (SoPlex) does far fewer resolves
   than phase 1 but each costs ~275x a CPLEX resolve, making it as expensive
   as all of phase 1 combined; batching more columns into phase 2 (tried,
   measured, reverted) made this *worse*, not better. The likely real
   explanation is architectural: the official binary uses Gurobi as an
   intermediate-precision solver and only switches to SoPlex once reduced
   costs are already small (`official-reference-BCCF/README.md`), rather
   than running a fully separate all-SoPlex phase 2. This is now
   `docs/ROADMAP.md` item 1 -- a genuine redesign, not attempted same-session
   given the certification-correctness stakes. Full write-up and the
   profiling table: `docs/STATUS.md`'s 2026-08-07 "SR3 dominance
   correctness fix + general speed gap investigated" checkpoint.
2. [Partially done, 2026-08-06] Read `BPPS_BP_TREE.cpp`/`.h` and
   `BPPS_BP_MAPPING.cpp` in full. Fathoming already matched legacy; fixed the
   branching tie-break (largest combined weight on equally-fractional pairs,
   BPPS_BP_TREE.cpp:314), fixed `BranchAndPriceResult::optimal` to depend
   only on the search queue emptying, and added a certified tree-wide
   `lower_bound` plus matching CLI output. See `docs/STATUS.md` refactoring
   checkpoint for full detail and legacy line references. [2026-08-06 late
   night] (a) node selection order is now partially addressed:
   `NodeStrategy::DepthFirst` (`src/search/branch_and_price.cpp`) matches
   the historical Together-first recursive traversal and threads a
   cross-tree warm-start pattern pool through the recursion
   (`ColumnGenerationOptions::warm_start_patterns`), instead of every node
   starting column generation from scratch. This is *not* the same
   mechanism as legacy's persistent-master row/bound toggling (each node
   still builds its own fresh `CplexRmp`, just pre-seeded with a much
   larger, filtered pattern pool) — see the `NodeStrategy::DepthFirst` doc
   comment in `include/bpp/branch_and_price.hpp` for the exact trade-off.
   `NodeStrategy::BestBound` remains the default and is unaffected. [Validated,
   2026-08-07] `201_2500_NR_41.txt` (real ANI-201 instance needing branching)
   now converges under both strategies with `--branch-price 5`/`30`, see the
   "Top priority, resolved" section above. (b) super-item merging — legacy
   shrinks the pricing DP subproblem per node via a persistent
   `MAPPING_UPDATE` structure kept along the root-to-node path. The new
   pricer contracts `Together` groups and enforces `Different` via a
   conflict list (`build_branch_groups`,
   `src/pricing/floating_root_pricer.cpp`), used by both the DFS reference
   and, as of 2026-08-07, the batched sparse DP
   (`price_label_setting_with_branching_and_sr3`) that replaced the DFS on
   the hot path — it still rebuilds the grouping from scratch on every
   pricing call rather than maintaining it incrementally across nodes, and
   the RMP/pattern representation itself is never contracted, but the
   dominant cost (DFS vs. DP) is fixed. Do not import the BPPS-only
   bilevel/strong-branching switch into classical BPP.
3. [Diving and stabilization done; LP-heuristic scheduling still open] Diving,
   LP-heuristic and stabilization trigger points were read in full
   (`BPPS_BP_DIVING.cpp`/`BPPS_BP_LP_HEUR.cpp`/`BPPS_BP_MAGIC_STAB.cpp` plus
   their `BPPS_BP_MASTER.cpp`/`BPPS_BP_TREE.cpp` call sites) and documented
   with line references in the `docs/STATUS.md` refactoring checkpoints.
   Diving was ported 2026-08-06 night (`DivingDriver`,
   `BranchAndPriceOptions::diving_enabled`). Stabilization was ported
   2026-08-07 (`run_floating_pricing_loop`'s static smoothing +
   misprice-safeguard block, `ColumnGenerationOptions::dual_stabilization`):
   verified against `BPPS_BP_MASTER.cpp`'s `SMOOTHING_*` routines (Wentges-
   style convex-combination smoothing fed only to the pricer, plus a
   misprice check against the real duals), gated to the root/cut-free case
   exactly like legacy, and confirmed correct on real ANI-201 data
   (`--root-cg --stabilization` on `201_2500_NR_0.txt`: same certified
   optimum, fewer iterations). Left opt-in and off by default, matching
   every historical parameter file found (including the `paper_v*` ones)
   having `PARAM_SMOOTH=0` -- this was never exercised in producing the
   paper's reported results either. **Still open**: the LP-heuristic
   root/non-root scheduling distinction (`BPPS_BP_LP_HEUR.cpp`) -- the basic
   heuristic itself already matches legacy's per-iteration frequency (see
   item 4's dive_master_solution entry below), what's missing is legacy's
   distinct root-vs-non-root trigger conditions.
4. [Partially done, 2026-08-07] Ran `--root-cg` across 15 real ANI-201
   instances (`tests/results/ani201-15-final.csv`): 15/15 converge,
   UB-correct, certified LB, mean 27.2s. Found (2026-08-06 late night) that
   5/5 sampled ANI-402 instances timed out at 120s even on the SR3-free
   `--legacy-root-cg` baseline. **Fixed (2026-08-07):** profiled the legacy
   executable on the same instance (`bpp-solve-legacy ... 402_10000_NR_0.txt`,
   31.9s, `number_of_EXACT_DP_CALLS 988 avg 0.0059s, label_DP_exact_avg
   132.7`) and found it uses a genuine sparse label-setting DP with
   dominance (`DP.cpp` → external `mckpsc_ls_main`), not a dense array —
   `FloatingRootPricer::price_label_setting`/`price_label_setting_with_sr3`
   used `std::vector<double> best(capacity+1)`, so cost scaled directly
   with capacity (7552 for ANI-402 vs. 2456 for ANI-201) on top of item
   count. Replaced with a sparse, dominance-pruned label frontier plus a
   tight fractional-knapsack fathoming bound
   (`FractionalBoundTable`, `src/pricing/floating_root_pricer.cpp`); see
   `docs/STATUS.md`'s 2026-08-07 checkpoint for the two implementation
   pitfalls hit along the way (calling `consider()` before pruning; a bound
   too loose to prune anything). `402_10000_NR_0.txt` and
   `402_10000_NR_1.txt` now both converge (~87-90s each, UB=133 matching
   legacy exactly) via `--legacy-root-cg`; ANI-201 re-verified unaffected
   (same `lp_bound`/`iterations`, ~2s slower wall time — an acceptable
   constant-factor cost for the tighter bound). **Still open:** `--root-cg`
   (two-phase, automatic SR3) on ANI-402 was not yet timed to convergence
   (>90s); the SR3 frontier's ~3x-per-simultaneous-cut growth (item 1 above)
   is unchanged by this fix — the tighter bound helps but does not add
   cross-cut-state dominance. Still needed: the full ANI201/ANI402 exact
   regression comparing integer UB/LB, nodes, triplets, columns, iterations
   and time against the historical executable, and a broader ANI-402 sample
   (only 2 instances tested so far) — practical to attempt now on both
   families, unlike before this fix.
5. [Resolved, 2026-08-07] The publication status of `legacy/` (not just the
   IBM/Gurobi vendor headers, the entire historical reference implementation)
   is decided: it stays local-only, never published. `.gitignore` now
   excludes `/legacy/` wholesale (previously only the 3 vendor headers plus
   build artifacts were excluded, which would have let the rest of the
   historical source — `dp_master.h`, `ip.h`, `mckpsc-ls.h`, the
   `BPPS_BP_*.cpp` implementation, etc. — be staged). Before any `git init`/
   first commit, still re-verify with `git status`/`git ls-files` that
   nothing under `legacy/` is staged, as a defense-in-depth check on the
   gitignore rule itself.

## Verified evidence

Clean default build in the ignored local `build/` directory passed:

```sh
cmake -S . -B build -DBPP_BUILD_TESTS=ON -DBPP_ENABLE_GMP=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

The previous CPLEX-only diagnostic and CPLEX+SoPlex builds passed
`bpp-core-tests` before local build artifacts were removed. CMake now rejects
`BPP_ENABLE_CPLEX=ON` without `BPP_ENABLE_SOPLEX=ON`; only the CPLEX+SoPlex
build is supported for the numerically-exact target.

Ten ANI201 root results:

- no-populate floating root: 10/10 UB equality, 10/10 valid global LP bounds,
  mean new wall time 4.765 s;
- SoPlex two-phase root: 10/10 UB equality, 10/10 valid global LP bounds,
  phase-I mean 388.4 iterations, phase-II mean 36.2 iterations, mean 10.193 s;
- these are root pricing certificates, not integer-optimality proofs when
  `ceil(LB) < UB` (typical sample: LB=65, UB=66).

The current local exact build is `build-cplex-soplex/`; it was configured with
CPLEX, SoPlex and GMP, builds both new and legacy executables, and passes
`ctest`. Its automatic-SR3 integration test verifies that a violated triplet
is generated and retained in the root result.

The paper reports for ANI-201: 50/50 exact integer solutions and 13.6 s mean
complete BCCF time (Table 1), 56.20 phase-I iterations and 279.7 phase-I
columns (Tables 2--3). The current root results are not yet aligned with the
complete paper workflow because automatic triplets, full populate scheduling
and the complete tree are not active.

## Important files

- `include/bpp/column_generation.hpp`: options/results, including populate and
  phase counters.
- `src/master/column_generation.cpp`: floating phase, safe phase, populate and
  automatic SR3 column-and-row restarts.
- `src/cuts/sr3.cpp`: current triplet separation utility.
- `src/pricing/floating_root_pricer.cpp`: pricing, branching and SR3 pricing.
  `price_label_setting`/`price_label_setting_with_sr3` are the production
  batched oracles, both a sparse dominance-pruned label frontier (`Label`,
  `merge_prune_frontier`) with an `O(log n)` fractional-knapsack fathoming
  bound (`FractionalBoundTable`) — do not reintroduce a dense
  `vector<double>(capacity+1)` DP, that was the ANI-402 bottleneck fixed
  2026-08-07. `price_with_sr3`/`price`/`price_with_branching_and_sr3` (DFS)
  are kept only as correctness references and overflow fallbacks, not called
  from the production hot path. `price_label_setting_with_branching_and_sr3`
  (added 2026-08-07) is the batched sparse DP for branching nodes,
  built via the shared `build_branch_groups` helper (Together contraction +
  Different conflict list); do not reintroduce the DFS on the
  `run_floating_pricing_loop` hot path, that was the branching-node pricer
  bottleneck fixed the same day.
- `include/bpp/cplex_rmp.hpp`/`src/master/cplex_rmp.cpp`: `CplexRmp::add_cut`
  appends one SR3 row to the live LP for a warm-started re-solve; do not
  reintroduce "construct a fresh CplexRmp per SR3 round" — that was the
  first of the two bottlenecks fixed 2026-08-06 night (see docs/STATUS.md).
  `CplexRmp::solve_mip_at_most` (added 2026-08-07) is a separate, one-shot
  0/1 MIP solve (changes column types to binary, adds a bin-count row) for
  `try_safe_mip_certification` -- has its own 20s `CPX_PARAM_TILIM`, not
  meant to be reused as an LP-relaxation master afterward.
- `include/bpp/pricing.hpp`/`src/pricing/floating_root_pricer.cpp`:
  `price_scaled_integer_with_sr3`/`price_scaled_integer_with_branching_and_sr3`
  (added 2026-08-07) reuse `price_label_setting_with_sr3`/
  `..._with_branching_and_sr3` verbatim, fed exact-integer-valued `double`s
  instead of real floating duals (Algorithm 1's `(p_int, r_int)`) -- this
  is why both functions' fractional-bound pruning now carries a
  `bound_margin` term (absorbs the bound's own floating-point rounding so
  it stays sound at any integer scale `K`, not just `bin_cost_=1`); do not
  remove that margin.
- `include/bpp/safe_bound.hpp`/`src/master/safe_bound.cpp`:
  `SafeBound::fathoming_bound` (added 2026-08-07) computes Algorithm 1's
  `LBF` (Proposition 3) in exact GMP rational arithmetic.
- `include/bpp/gurobi_rmp.hpp`/`src/master/gurobi_rmp.cpp`: `GurobiRmp`,
  added 2026-08-07, mirrors `CplexRmp`'s contract exactly (same historical
  primal-simplex/single-thread justification) as an alternative RMP
  backend, built with `-DBPP_ENABLE_GUROBI=ON -DGUROBI_ROOT=...`.
  `include/bpp/master_rmp.hpp`/`src/master/master_rmp.cpp`: `MasterRmp`
  (`std::variant<CplexRmp, GurobiRmp>`) and the `LpBackend` enum
  (`ColumnGenerationOptions::backend`, default `Cplex`) are what let
  `column_generation.cpp` stay backend-agnostic; `bpp-solve --solver
  cplex|gurobi` selects at the CLI layer, defaulting to whichever backend
  the binary was built with. See docs/STATUS.md's "Gurobi backend restored"
  checkpoint for why (reverses the earlier "Gurobi removed by design"
  decision, per explicit instruction) and the official BCCF reference
  comparison (`../official-reference-BCCF/`, outside this repository) this
  session also added.
- `include/bpp/branch_and_price.hpp`/`src/search/branch_and_price.cpp`:
  `NodeStrategy::BestBound` (default, `solve_branch_and_price_best_bound`)
  and `NodeStrategy::DepthFirst` (`solve_branch_and_price_depth_first`,
  `DepthFirstDriver`) tree drivers.
- `src/cli/main.cpp`: `parse_args` accepts `INSTANCE`, `MODE`
  (`--legacy-root-cg`/`--no-populate`/`--root-cg`/`--populate`/`--branch-price`),
  one legacy-positional integer, and named flags (`--strategy`,
  `--sr3-gap-activation`, `--sr3-max-cuts`, `--diving`/`--diving-down-budget`/
  `--diving-time-limit`, `--stabilization`/`--stabilization-alpha`) in any
  order; `--help`/`-h` prints full usage and exits. `print_help()`'s text
  must be kept in sync by hand whenever a flag is added or changed.
- `tests/test_unit.cpp` / `test_integration.cpp` / `test_regression.cpp`:
  three CTest targets (`bpp-unit-tests`/`bpp-integration-tests`/
  `bpp-regression-tests`, split 2026-08-07 from the former monolithic
  `tests/test_core.cpp`/`bpp-core-tests`) -- see each file's header comment
  for its exact scope before adding a new test to any of them.
- `scripts/run_ani_comparison.sh`: old/new comparison runner; its CSV column
  `old_lp_bound` is historical diagnostic LP, while `new_lb` is certified only.
- `tests/results/ani-baseline.csv`: normalized two-row old ANI baseline.
- `tests/results/ani-comparison.csv`: 100-row bounded smoke comparison; all
  rows are iteration-limited and therefore have `new_lb=NA`.
- `tests/results/ani201-10-comparison.csv`: ten-instance no-populate sample.
- `tests/results/ani201-10-root-soplex-comparison.csv`: ten-instance SoPlex
  root sample.
- `tests/results/ani201-15-final.csv`: fifteen-instance ANI-201 sample after
  the SR3 performance fix, all converged and certified.
- `tests/results/ani402-5-final.csv`: five-instance ANI-402 sample, all 5
  timed out at 120s — evidence for the still-open scaling gap (item 4 above).
- `docs/STATUS.md`: canonical progress report and GitHub-readiness assessment.
- `docs/paper-comparison.md`: paper Table 1--3 recalculation.
- `docs/legacy-info/`: long historical `info_*` logs and the two-row
  `ani-baseline-info_*` exports from the directory beside the paper.

## Cleanup state

Per-run logs, object files and backup archives were moved, not destroyed, under
the following quarantine; the reproducible ignored build directories are kept
locally for immediate execution:

`/tmp/bpp-numerically-exact-cleanup-20260806`

This quarantine includes the old project logs/builds and the original
`info_*` files that were beside the paper. The canonical copies are now under
`docs/legacy-info/`; the paper directory no longer contains those files. The
ignored local build directories `build/` and `build-cplex-soplex/` now hold the
portable and CPLEX+SoPlex executables respectively; both pass `ctest`.

## Safe continuation protocol

1. Read this file, `docs/STATUS.md`, `docs/legacy-feature-inventory.md` and
   `docs/paper-comparison.md`.
2. Inspect existing code/tests before editing. Keep generated build products in
   ignored `build*` directories or `/tmp`, never in tracked source folders.
3. Build SoPlex in an external directory and pass it to CMake:
   `-DSOPLEX_BUILD_ROOT=/tmp/soplex-build-large`; CMake rejects CPLEX without
   SoPlex.
4. Validate automatic SR3 separation with a regression test proving that a
   violated triplet is generated, inserted in the RMP and reflected in
   pricing/safe-bound state.
5. Re-run the clean default tests, then CPLEX+SoPlex builds if available.
6. Update this file, `docs/STATUS.md` and the comparison report with measured
   results. Never label a root LP certificate as an integer optimum.
