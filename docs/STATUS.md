# Implementation status

## Completed

- New project created independently of the historical archive (a
  local-only, never-published reference implementation, see `legacy/` and
  `.gitignore`).
- Portable CMake build and install target.
- RAII BPP instance model with validation of capacity and weights.
- Strict minimal instance reader (`n capacity`, followed by `n` weights).
- Solution representation and complete packing validator.
- CLI smoke solver and unit test.
- No Gurobi dependency or source reference in the new project. **Reversed
  2026-08-07**: Gurobi was restored as an explicit, opt-in, alternative
  floating-point LP backend to CPLEX (`include/bpp/gurobi_rmp.hpp`,
  `-DBPP_ENABLE_GUROBI=ON`), per explicit instruction. See the 2026-08-07
  "Gurobi backend restored" checkpoint below.
- Historical BPP backend builds with CPLEX, SoPlex and GMP through CMake.
- CPLEX-side normalization of historical parameter files: legacy values
  selecting Gurobi are mapped to CPLEX before master/pricing initialization;
  this does not remove the mandatory SoPlex/GMP safe phase.
- ANI-201 (`201_2500_NR_0.txt`) completes the root solution after the RMP
  alignment fix: LP 65.000004618703, incumbent 66, 3725 columns, 2.49 s
  algorithm time (2.90 s wall-clock run).
- Historical no-populate baselines for ANI201 and ANI402 are recorded in
  `tests/results/ani-baseline.csv`; the all-instance runner is
  `scripts/run_ani_comparison.sh` and the methodology is in
  `docs/ani-comparison-report.md`.
- The archive contains distinct no-SoPlex/no-populate and
  SoPlex/populate parameter families; their mapping and the two corresponding
  `info_*` baselines are documented in `docs/legacy-parameter-matrix.md` and
  `docs/legacy-info-files.md`.
- Root pattern population is available through `--populate [MAX_COLUMNS]` and
  reports `populate_columns`/`populate_complete`; after columns are
  enumerated, the exact SoPlex phase is rerun so the final safe certificate
  covers the enlarged master. The no-populate baseline is unchanged.
- Per-run logs, object files and historical backup archives were moved out of
  the source tree; reproducible local builds are kept in ignored `build/` and
  `build-cplex-soplex/` directories, while CSV summaries remain under
  `tests/results/`.

## Refactoring checkpoint (2026-08-06, SR3 gate alignment)

- `solve_root_column_generation` now enforces the two historical SR3
  activation gates identified in `BPPS_BP_MASTER.cpp` (via a full read of
  `BPPS_BP_TRIPLETS.cpp/.h`): a gap-based activation gate
  (`ColumnGenerationOptions::sr3_gap_activation`, legacy
  `PARAM_TRIPLET_GAP_ACT`, BPPS_BP_MASTER.cpp:4705) and a cumulative
  cut-budget gate (`max_sr3_cuts_total`, legacy `PARAM_MAX_TRIPLETS`,
  BPPS_BP_MASTER.cpp:4701/4712). Both default to values that reproduce the
  previous unconditional-separation behavior (`sr3_gap_activation = inf`,
  `max_sr3_cuts_total = 1000`), so existing callers are unaffected unless they
  opt into a tighter setting.
- New regression tests prove: (1) separation stays off when the
  incumbent/LP gap is not below `sr3_gap_activation` even though a violated
  triplet exists, and (2) separation stops once `max_sr3_cuts_total` is
  reached while still returning a converged relaxation, matching the
  historical "skip the triplet block, keep solving" behavior rather than
  failing the node.
- Both the portable (`build/`) and CPLEX+SoPlex (`build-cplex-soplex/`)
  configurations build and pass `ctest` with these changes.

Not yet done (tracked for the tree/scheduling phases below, since they need
shared state across B&B nodes that does not exist yet):
- The historical cut budget and gap gate are legacy-global (persist for the
  whole branch-and-price tree, accumulated in `bp_data->count_triplets`).
  The new gates are scoped to a single `solve_root_column_generation` call;
  `--branch-price` does not yet thread a running cut count or a shared
  incumbent-aware gap check across nodes.
- The historical `PARAM_TRIPLET_OFF_FOR_DIVING` skip has no analog yet
  because the new code does not have a separate diving sub-search phase (it
  applies rounding/diving heuristics inline every iteration instead).
  CLI flags for the new options are not exposed yet; they are
  library/options-level only.

## Refactoring checkpoint (2026-08-06, tree fathoming/optimality alignment)

Read `BPPS_BP_TREE.cpp` (4342 lines), `BPPS_BP_TREE.h` and
`BPPS_BP_MAPPING.cpp` in full to compare against
`src/search/branch_and_price.cpp`. Findings and the resulting changes:

- **Fathoming rule**: already matched (`ceil(safe_bound) >= incumbent_bins`);
  no change needed. The new code's rational-exact `ceil_bins()` is actually
  stronger than the legacy epsilon-adjusted `my_ceil`, consistent with the
  numerically-exact goal.
- **Branching pair selection**: the "closest to 0.5" rule already matched;
  added the missing legacy tie-break (BPPS_BP_TREE.cpp:314): among equally
  fractional pairs, prefer the larger combined item weight, instead of
  keeping whichever pair the enumeration order found first
  (`src/search/branching.cpp`).
- **Optimality-certificate correctness**: fixed `result.optimal` to depend
  only on the queue being empty (a complete, certified search), not also on
  `processed_nodes < max_nodes`, which could under-report optimality on the
  rare exact coincidence of the queue emptying on the last permitted node.
  `BranchAndPriceResult` now exposes a certified `lower_bound` (a `SafeBound`
  equal to `incumbent_bins` when optimal, or the best remaining queued node's
  bound otherwise via a new `BestBoundQueue::peek()`), and the CLI
  `--branch-price` output prints `lower_bound_safe`,
  `lower_bound_safe_ceil` and `integer_optimum_certified`, matching the root
  command's output contract.
- **Fathoming counter completeness**: nodes whose relaxation is already
  integral (no fractional pair) are now counted in `pruned_nodes`, matching
  the legacy convention of counting every fathomed node, not only
  bound-pruned ones.
- Both build configurations (`build/`, `build-cplex-soplex/`) build and pass
  `ctest` with these changes, including new regression tests for the
  tie-break and for the certified lower bound on a fully-solved search.

**Deliberately not done in this pass** (larger, riskier structural work,
documented here rather than attempted half-finished):
- **Node selection order**: the legacy tree is plain recursive DFS
  (Together-branch-first dive with backtracking, BPPS_BP_TREE.cpp:3042-4308),
  not a best-bound priority queue. The new code keeps best-bound ordering.
  This is a legitimate algorithmic choice (both are complete and correct
  branch-and-bound strategies; they differ in node visiting order, not in
  correctness of the final answer), but it means per-node/per-instance node
  counts will not match the legacy trace.
- **Super-item merging**: legacy recomputes a super-item mapping
  (`MAPPING_UPDATE`, BPPS_BP_MAPPING.cpp) at every node from the path's
  Together/Different constraints and shrinks the pricing DP subproblem
  accordingly; un-merging happens when a DP pattern is expanded back to
  per-item RMP columns. Correction to an earlier version of this note: the
  new code already does the equivalent contraction for pricing —
  `FloatingRootPricer::price(instance, duals, branching)` and
  `price_with_branching_and_sr3`
  (`src/pricing/floating_root_pricer.cpp:324-488,490-658`) union-find the
  `Together` constraints into weight/value-summed `Group`s and run the
  knapsack branch-and-bound over groups, with a conflict matrix between
  groups enforcing `Different`; `BranchingState::accepts` is only a final
  safety assertion (line 484/653), not the enforcement mechanism.
  `Group::items` already stores original item ids, so results need no
  separate un-merge step. What is genuinely still missing: (a) the grouping
  is recomputed from scratch on every pricing call via a fresh union-find
  over `branching.constraints()`, instead of being maintained incrementally
  in a structure persisted along the root-to-node path like legacy's
  `MAPPING_UPDATE` — correctness-equivalent, but recurring cost instead of
  amortized, which may matter on deep ANI-402 trees; (b) the RMP/pattern
  representation itself is never contracted (only the pricing subproblem
  is), so master size does not shrink the way it can in legacy.
- **Column warm-start**: legacy solves every node against one persistent
  master/column pool built up over the whole search; the new code re-solves
  a fresh two-phase column generation per node. This is a genuine semantic
  simplification (documented already in `docs/CONTINUATION_STATE.md`), not
  merely a refactor, and is the main reason node-by-node timing/iteration
  counts cannot yet be compared 1:1 with the legacy executable.

## Refactoring checkpoint (2026-08-06, stabilization/diving/LP-heuristic scheduling review)

Read the trigger points in `BPPS_BP_DIVING.cpp` (1040 lines), `BPPS_BP_LP_HEUR.cpp`
(761 lines) and `BPPS_BP_MAGIC_STAB.cpp` (626 lines), cross-referenced with
their call sites in `BPPS_BP_MASTER.cpp` and `BPPS_BP_TREE.cpp`, and compared
them with the current inline heuristics in `src/solver/heuristics.cpp` and
`src/master/column_generation.cpp`.

Findings:
- **Diving** is a genuinely separate recursive sub-search
  (`diving(inst,bp_data,0)`, BPPS_BP_TREE.cpp:3603-3741): it sets a
  `DIVING_ACTIVE` flag that disables SR3 separation
  (`BPPS_BP_MASTER.cpp:4698`) and can optionally deactivate all active
  triplets first (`PARAM_TRIPLET_OFF_FOR_DIVING`), runs its own bounded
  search, then restores normal mode. The new code has no separate diving
  phase; `dive_master_solution`/`round_master_solution` in
  `src/solver/heuristics.cpp` are lightweight, stateless roundings applied
  inline after every column-generation iteration instead.
- **LP-based heuristic** (`master_LP_HEUR`, BPPS_BP_LP_HEUR.cpp:515) is
  gated by `PARAM_SUPER_LP_BASED_HEURISTIC`/`PARAM_LP_HEURISTIC` (levels 1-3,
  each trying a different column-loading strategy: `load_sol_1/2/3`) and
  behaves differently at the root (`level==0`, inserts new columns and
  re-optimizes the RMP, BPPS_BP_MASTER.cpp:4122-4166) than at deeper nodes
  (heuristic-only, no column insertion, line 4168).
- **Stabilization** ("Farley bound"/`MR_FARLEY`) is gated on root vs
  non-root (`PARAM_MR_FARLEY_ROOT_NODE` vs `PARAM_MR_FARLEY`,
  BPPS_BP_MASTER.cpp:4453-4459) and on `DIVING_ACTIVE`, and turns itself off
  once `optimality==true` is reached (`STABILIZATION=0`,
  BPPS_BP_MASTER.cpp:4566-4571) rather than running for a fixed number of
  iterations. The new code's `dual_stabilization`/`stabilization_alpha`
  option (`ColumnGenerationOptions`) is a plain fixed-weight moving average
  of duals applied every iteration when enabled, with no phase/level gating
  and no automatic stop condition; it is already disabled by default
  precisely to avoid silently diverging from raw exact dual pricing (see the
  existing note in `include/bpp/column_generation.hpp`).

**Decision: defer a structural port of this phase.** Unlike the SR3 gates
and tree fathoming/optimality fixes above, these three components are not
independent, narrowly-scoped parameters — they are separate control-flow
phases (a nested sub-search, a level-dependent column-loading heuristic, and
a self-terminating dual-smoothing phase) intertwined with legacy globals
(`DIVING_ACTIVE`, `STABILIZATION`, `level`) that the new architecture does
not have. Porting them correctly requires either introducing that phase
machinery (risking new bugs in the already-verified safe-bound/pricing path)
or reinterpreting their intent for the new inline-heuristic design — both
are exactly the kind of algorithmic change the project's own gate discipline
(`PIANO_REFACTORING_BPP.md`, phase 4) requires to be done one component at a
time with before/after regression comparison, not folded into an unrelated
session. No code was changed for this phase; this checkpoint exists so the
next session does not need to re-derive these trigger points from the
2427-line legacy source again.

## Refactoring checkpoint (2026-08-06, legacy header licensing)

Inspected `legacy/include/cplex.h`, `cpxconst.h` and `gurobi_c.h`: all three
are unmodified vendor SDK headers with no redistribution grant (`cplex.h`/
`cpxconst.h`: "Licensed Materials - Property of IBM ... Copyright IBM
Corporation 1988, 2019. All Rights Reserved."; `gurobi_c.h`: "Copyright 2020,
Gurobi Optimization, LLC"). They were previously untracked by `.gitignore`
and would have been committed as soon as the project directory was
initialised as a Git repo. Fixed: added the three files to `.gitignore` and
documented the requirement (`legacy/include/README.md`) that each user
supply their own copy from an installed CPLEX/Gurobi SDK to build the local
`bpp-solve-legacy` reference executable — the same externally-installed-SDK
model the public solver already uses for `CPLEX_ROOT`. The files remain on
disk for local builds; only their Git tracking status changed. The other
headers in `legacy/include/` (`dp_master.h`, `ip.h`, `mckpsc-ls.h`, etc.) are
the algorithm authors' own historical code with no vendor restriction, but
their publication license is still unresolved (unchanged from before,
tracked as `CONTINUATION_STATE.md` item 5). `legacy/soplex-5.0.1/` already
carries its own `COPYING` (ZIB academic license) and needed no change.

## Refactoring checkpoint (2026-08-06, night: SR3 performance fix — root now closes real ANI-201 instances)

The performance gap flagged in the previous two checkpoints (SR3-enabled
`--root-cg` not converging within 120s on a real ANI-201 instance) is fixed.
Two independent bottlenecks were found and fixed, in this order:

1. **Master rebuilt from scratch every SR3 round.** `solve_root_column_generation`
   used to call `solve_root_column_generation_once` fresh for every
   separation round, which rebuilt `CplexRmp` from nothing and re-added every
   already-known pattern one column at a time, then re-ran the whole CG loop
   from iteration 0. Fixed: a single `CplexRmp`/pattern pool now lives for
   the whole call; `CplexRmp::add_cut(cut, patterns)` appends one row via
   `CPXnewrows`/`CPXchgcoeflist` on the live LP instead of rebuilding, so
   `solve()` warm-starts from the previous basis — mirroring the historical
   `CPXaddrows`-into-the-persistent-master approach
   (`BPPS_BP_TRIPLETS.cpp:557-651`). `run_floating_pricing_loop` factors the
   shared per-iteration pricing body out of both `solve_root_column_generation_once`
   and the new persistent-master path, driven by cuts read live from
   `master.sr3_cuts()` rather than from the (now largely vestigial once
   auto-separation is on) `options.sr3_cuts`.
2. **The SR3-aware pricer was an exponential-ish DFS.** Once even one cut
   was active, pricing switched from `price_label_setting` (capacity-indexed
   DP with dominance, batched candidates) to `price_with_sr3`, a recursive
   DFS over all items pruned only by a fractional-knapsack bound, returning
   one column per call. On real ANI-201 (201 items, capacity 2456) this
   never terminated within 176+ seconds. Fixed: `FloatingRootPricer::price_label_setting_with_sr3`
   extends the label-setting DP with one extra per-label state per active
   cut — how many of its three items (clamped to 2) are already in the
   partial pattern — and adds the cut's dual exactly once, the step the
   count crosses from 1 to 2 (matching `Sr3Cut::coefficient`). This mirrors
   legacy's `DP.cpp:prepare_data_cuts_DP_LABEL_SETTING`, which folds SR3
   duals into the same efficient DP instead of falling back to a weaker
   algorithm. `price_with_sr3` (the DFS) is kept only as a correctness
   reference for tests; production code (`run_floating_pricing_loop`, the
   SoPlex safe phase, the CPLEX-only phase-2 fallback) now calls the DP.

   Implementation detail that mattered as much as the algorithm: the first
   version of this DP keyed labels by `(load, vector<uint8_t> cut-state)` in
   a `std::map`, which heap-allocates a vector on every label transition —
   with thousands of transitions per call this made the "fix" slower than
   the DFS it replaced. Packing the cut-state into 2 bits per cut inside a
   single `std::uint64_t` (`(load << 40) | packed_state`, keyed in a
   `std::unordered_map`, capped at 20 simultaneous cuts and capacity < 2^24)
   removed that allocation and made the DP fast in practice.

   The DP's state space still grows roughly 3x per additional *simultaneously
   active* cut (measured on the same ANI-201 instance: ~0.1s per pricing
   call at 1 cut, ~0.5s at 4, ~3.4s at 6, ~31s at 9) — expected, since each
   cut adds a 3-valued dimension and there is no cross-state dominance
   pruning yet (only exact-state memoization). `ColumnGenerationOptions::max_sr3_separation_rounds`
   default was lowered from 10 to 4 to keep the common case fast and
   predictable; raising it is safe but costs roughly 3x DP time per extra
   round, until real dominance pruning removes that ceiling (documented as
   future work, not attempted this session).

   A related bug was fixed in the same pass: when the round budget was hit,
   the code forced `result.converged = false` unconditionally, discarding a
   relaxation that had, in fact, already converged (and could already be a
   valid, even integer-optimal, certified bound) just because the
   *separation* loop stopped early. Fixed to keep whatever `converged` value
   pricing itself established — hitting the round budget is a cost safety
   valve on further cutting, not a pricing failure.

**Measured before/after on real ANI-201 instances** (`build-cplex-soplex/bpp-solve`,
`--root-cg 2000`, `tests/results/ani201-5-post-dp-fix.csv`):

| | before this fix | after this fix |
| --- | --- | --- |
| `201_2500_NR_0.txt` root (SR3 on) | did not converge in 120s | converged in 16.7s, `integer_optimum_certified=1` |
| 5-instance ANI-201 sample, `--root-cg` | 0/5 converged (all iteration-limited or hung) | 5/5 converged, 5/5 UB equal, 5/5 valid certified LB, mean 16.4s |
| `201_2500_NR_0.txt`, `--branch-price 50` | did not finish 50 nodes in 45s | `status optimal`, 1 node, `integer_optimum_certified=1`, 16.5s |

For reference, the paper reports 13.6s mean for the **complete** BCCF
algorithm (root + full tree, Table 1) on ANI-201; the measurements above are
root-only (branch-and-price happened to close at the root on the sampled
instances) and on a different machine, so this is encouraging but not a
claim of matching the paper's benchmark — the ANI-201/ANI-402 full sweep
(`docs/CONTINUATION_STATE.md` item 4) is still required, now that it is
finally practical to run.

## Refactoring checkpoint (2026-08-06, late night: DFS node strategy, CLI flags, larger-sample validation)

**Depth-first node strategy.** Added `NodeStrategy` (`include/bpp/branch_and_price.hpp`):
`BestBound` (the existing priority-queue driver, unchanged, still the
default) and `DepthFirst`, a new recursive driver
(`src/search/branch_and_price.cpp`) that always explores the Together child
before Different, with backtracking, matching the historical tree's
traversal order (`BPPS_BP_TREE.cpp`). A `std::set<std::vector<int>> pool_`
accumulates every pattern priced anywhere in the tree; each node seeds its
own column generation from the subset of that pool feasible under its own
Ryan-Foster constraints
(`ColumnGenerationOptions::warm_start_patterns`, plumbed through
`initialize_pattern_pool` in `src/master/column_generation.cpp`), instead of
starting from just the singleton patterns every time. This is a deliberate,
documented simplification of the historical persistent-master/row-toggling
warm start (see the `NodeStrategy::DepthFirst` doc comment): it reuses most
of the practical benefit (thousands of already-priced columns are filtered
and reused, not rediscovered) without needing to replicate CPLEX row
activation/deactivation across backtracking. Both strategies are exact;
`BranchAndPriceResult::lower_bound` is only populated for an *incomplete*
depth-first run when the search proves optimality outright, since depth-first
has no ordered pending-node queue to read a partial certified bound from
(best-bound still reports one on early node-limit termination, unchanged).
New unit tests cross-check both strategies reach the same certified answer.

**CLI.** `src/cli/main.cpp` was rewritten from strict positional parsing to
a small `parse_args` that still accepts the one legacy-compatible bare
integer in its historical position, plus `--strategy best-bound|depth-first`
(only meaningful for `--branch-price`), `--sr3-gap-activation VALUE` and
`--sr3-max-cuts VALUE` (map to the two SR3 gates added earlier), in any
order, any mode. Invalid flag values now fail fast with a usage message
instead of being silently ignored. `README.md`'s CLI section was out of
date (`build-cplex-new` instead of `build-cplex-soplex`, missing the new
flags) and has been corrected.

**Repository hygiene found during this pass.** Five `info_*.txt` files (up
to 120KB) had accumulated at the project root as a side effect of running
`bpp-solve-legacy` repeatedly during testing; removed, and `/info_*.txt`
added to `.gitignore` so they cannot be committed if regenerated. Neither
file was source or baseline data — the canonical baseline snapshot is
`docs/legacy-info/ani-baseline-info_*`.

**Larger-sample validation.** Re-ran the SR3-performance-fixed root
(`--root-cg`) across 15 ANI-201 instances
(`tests/results/ani201-15-final.csv`): 15/15 converge, 15/15 UB equal to the
historical executable, 15/15 valid certified lower bound, mean 27.2s
(range 12.7s–61.2s; the paper reports 13.6s mean for the *complete* BCCF
algorithm root+tree on ANI-201, so this root-only sample is in the right
neighborhood but not yet at parity — the spread correlates with how many
simultaneous SR3 cuts a given instance needs, consistent with the DP's
~3x-per-cut growth already documented above). `--branch-price` with both
`NodeStrategy` values was run on 4 of those same instances: 4/4 agree
exactly between strategies (status `optimal`, incumbent 66,
`integer_optimum_certified 1`, 1 processed node, times within ~1s of each
other) — on this sample the root, once the SR3 fix was in place, already
proves optimality, so the tree (and therefore the traversal-order
difference between the two strategies) was not actually exercised at scale;
depth-first's recursion/backtracking/warm-start plumbing is otherwise only
covered by the smaller synthetic unit tests.

**New finding: ANI-402 does not yet converge, and it is not (only) the SR3
issue.** The same sweep on 5 ANI-402 instances (402 items, capacity ~7550;
`tests/results/ani402-5-final.csv`) timed out at 120s on all 5, `--root-cg`.
Isolating the cause: even the SR3-free baseline (`--legacy-root-cg`, the
path already confirmed fast on ANI-201) did not converge within 60s on one
ANI-402 instance either, with an unusually high system-time-to-real-time
ratio (`sys 3m2s` for `real 1m0s`) suggestive of heavy allocation/syscall
overhead, not just more arithmetic. This is a **separate, not yet
diagnosed** scaling problem in the base floating-phase pricing DP itself
(cost scales with items × capacity per iteration, and larger instances
plausibly also need more CG iterations to converge) — orthogonal to the
SR3-specific fix earlier in this document. Not investigated further this
session; see the priority list below.

## Refactoring checkpoint (2026-08-07: ANI-402 fixed — sparse label-setting DP)

The ANI-402 scaling gap flagged in the previous checkpoint (times out even
without SR3) is fixed. Root cause, found by profiling the legacy executable
on the same instance: `bp-solve-legacy` solves `402_10000_NR_0.txt`
(402 items, capacity 7552) in 31.9s, and its own instrumentation shows why —
`number_of_EXACT_DP_CALLS 988, avg time 0.0059s, label_DP_exact_avg 132.7`.
Legacy's pricer (`DP.cpp`, calling the external `mckpsc_ls_main` label-setting
solver) keeps on the order of 130 labels per call *regardless of capacity*,
because it is a genuine sparse label-setting DP with dominance pruning. The
refactored `price_label_setting` instead used a **dense array of size
capacity+1**, so cost scaled directly with capacity (7552 for ANI-402 vs.
2456 for ANI-201) on top of scaling with item count (402 vs. 201) — the
double hit that made it never converge.

**Fix** (`src/pricing/floating_root_pricer.cpp`): replaced the dense array in
both `price_label_setting` and `price_label_setting_with_sr3` with a sparse
frontier of `Label{load, value, path}`, sorted ascending by load and kept
non-dominated (`merge_prune_frontier`: label A dominates B when
`A.load <= B.load && A.value >= B.value`) — the standard 1D knapsack
label-setting technique, matching legacy's approach. Two implementation
pitfalls had to be fixed in turn before this actually paid off, both
instructive:

1. **First attempt was slower than the DFS it replaced.** Calling
   `consider()` (path reconstruction + `std::set` insert for dedup) on every
   raw extension *before* dominance pruning removed the hopeless ones was
   far more expensive than the dense array's approach. Fixed by only calling
   `consider()` on labels that both survive pruning and are new this
   iteration (tracked via a `new_node_floor` index into the path arena).
2. **The frontier still didn't shrink** (plateaued around 1550 labels on
   ANI-402, vs. legacy's ~130) because the fathoming bound
   (`suffix_positive`, "sum of every remaining item's dual, ignoring the
   capacity limit") was too loose to prune much on a large-capacity
   instance. Replaced with `FractionalBoundTable::bound(position,
   remaining)`, the classic fractional-knapsack LP relaxation bound
   (prefix sums of weight/value in ratio order, `O(log n)` per query via
   `std::upper_bound`), which *does* respect remaining capacity. Labels
   failing this bound are now dropped from the frontier permanently (the
   bound only shrinks as position advances), not just skipped for the
   current item — this is what actually keeps the frontier small. The same
   two fixes were applied to `price_label_setting_with_sr3` (permanent
   pruning per item, tight bound plus a flat optimistic cut-dual allowance).

**Measured result** (`402_10000_NR_0.txt` and `402_10000_NR_1.txt`,
`--legacy-root-cg 2000`, i.e. the SR3-free baseline used to isolate this from
the earlier SR3 fix):

| | before | after |
| --- | --- | --- |
| Converges? | No — times out at 60s+ | Yes, both instances tested |
| Time | n/a (never converges) | ~87–90s (legacy: 31.9s, so still ~2.8x slower) |
| UB | n/a | 133, matches legacy exactly on both instances |
| Iterations | n/a | 860 (`402_10000_NR_0.txt`) |

`--root-cg` (the two-phase driver with automatic SR3 separation) on the same
ANI-402 instance still does not converge within 90s — expected, since it
does strictly more work per round (safe-phase certification, multiple SR3
rounds) on top of an already-slower base case; not yet measured how much
more time it needs. ANI-201 was re-verified unaffected: same `lp_bound=65`,
same `iterations=377` as before, wall time ~6.9s (up from ~4.6s — the
tighter per-label bound query has its own, smaller, constant-factor cost,
a worthwhile trade for what it fixes on larger instances).

**Still open in the label-setting DP:** no cross-load dominance was added to
the *SR3* frontier across different cut-states (only within an exact
`(load, cut_state)` match, via the hashmap), so the ~3x-per-simultaneous-cut
growth documented in the SR3 performance checkpoint above is unchanged by
this fix — the fractional-bound tightening helps it (tighter fathoming
prunes more regardless of cut-state), but does not remove the combinatorial
ceiling. `price_with_sr3` and the plain dense-array knapsack in
`FloatingRootPricer::price`/`price_candidates` were intentionally left
alone (not on the hot path implicated by this finding; `price_with_sr3`
remains a deliberately-kept DFS correctness reference for tests).

## Refactoring checkpoint (2026-08-07 night: CPLEX threading fix closes most of the remaining performance gap, diving ported)

Continuing from the ANI-402 sparse-DP fix above, four more items were
investigated overnight, all evidence-driven (each is a legacy behavior
faithfully reproduced or a measured regression reverted, not a new
invented heuristic — see individual entries):

**1. SR3 cross-state dominance: tried, measured net-negative, reverted.**
Added exact componentwise dominance across different SR3 cut-states
(`prune_dominated_sr3_labels`, `cut_state_covers`): label A with
`load_a<=load_b`, `value_a>=value_b` and every cut-count component
`>=` B's provably dominates B (since which items to add next is always a
free choice, so any extension achievable from B is achievable from A with
an equal-or-better outcome). Measured on real ANI-201+SR3: the O(n^2) sweep
only ever removed ~10% of labels (e.g. 3598 -> 3271) while costing ~0.1s
per call, repeated every item once the frontier passed a 128-label
threshold — turned a 16.7s converging run into a 60s+ timeout. **Disabled**
(threshold raised to `SIZE_MAX`, i.e. never triggers) until backed by a
sub-quadratic structure (bucket-by-load skyline, not a flat pairwise scan).
The ~3x-per-simultaneous-cut frontier growth this was meant to fix is
therefore still present.

**2. `dive_master_solution` was O(bins x patterns), not O(patterns).**
Re-read `BPPS_BP_LP_HEUR.cpp`/`BPPS_BP_DIVING.cpp` in full (via a research
pass) to check the actual historical scheduling before changing anything,
per explicit user instruction not to invent behavior. Finding: legacy's
`PARAM_SUPER_LP_BASED_HEURISTIC=1` (the value used in `param_BPPS_BP_v1/v4.txt`)
already runs its LP-heuristic on **every** CG iteration, same as this
codebase's inline heuristics — so throttling frequency would have been an
invented deviation, not a fix. What legacy actually does differently:
`load_sol_1/2/3` (`BPPS_BP_LP_HEUR.cpp:190-403`) are each a **single sorted
pass** over columns, O(patterns). Our `dive_master_solution` instead
repeatedly rescanned the *entire* pattern pool once per bin selected
(`while(true){ for every column... }`), making it O(bins x patterns) — on
ANI-402 with ~8800 columns and ~130 bins that is a real, avoidable cost.
Rewritten to the same single-sorted-pass shape as
`round_master_solution`/legacy's `load_sol_1`.

**3. CPLEX was not configured the way the ANI parameter files configure it
-- the single biggest win.** `BPPS_BP_MASTER.cpp:1382-1383` and the ANI
parameter file (`PARAM_SIMPLEX=1`, `PARAM_CPU=1`) force
`CPX_PARAM_LPMETHOD=CPX_ALG_PRIMAL` and `CPX_PARAM_THREADS=1` on the RMP.
`CplexRmp` set neither, leaving CPLEX's automatic method/thread selection
in charge. Measured effect on `402_10000_NR_0.txt` (`--legacy-root-cg`,
isolating this from the SR3-specific work above): **CPLEX resolve time
41.5s -> 8.4s**, and `sys` time collapsed from **3m+ to 0.2s** (the
earlier huge `sys` time really was multi-thread synchronization overhead,
not I/O or memory pressure as first guessed) — total wall time **87s ->
48.5s**. Added the same two `CPXsetintparam` calls, with a comment citing
the exact legacy line numbers, to `CplexRmp`'s constructor.

**4. DP buffer reuse.** Minor, included for completeness: `extended`,
`merge_scratch` and the pruned-frontier output in
`price_label_setting`/`merge_prune_frontier` are now caller-owned buffers
cleared and reused every item instead of freshly allocated vectors —
measured effect alone was small (DP time 39.7s -> 38.2s on the same
instance) compared to the CPLEX fix, but free once the API was already
being touched.

**5. Diving implemented, faithfully scoped.** Read `BPPS_BP_DIVING.cpp` in
full. Legacy's diving triggers exactly once per solve
(`level==0 && PARAM_TOKEN_DIV>-1`, `BPPS_BP_TREE.cpp:3565`), not per node
and not on a frequency: a bounded DFS that always dives into the Together
branch for the fractional pair the LP already leans towards (the "IJOC"
rule — maximum co-occurrence value, *not* the tree's closest-to-0.5 rule)
and opens at most `PARAM_TOKEN_DIV` (default 1) Different branches at once.
Ported as `DivingDriver` (`src/search/branch_and_price.cpp`) and a new
`select_most_confirmed_pair` (`src/search/branching.cpp`, distinct from
`select_fractional_pair` used by tree branching). Exposed as
`BranchAndPriceOptions::diving_enabled`/`diving_down_budget`/
`diving_time_limit_seconds` (default off, matching neither v1/v4's
"on" default nor the ANI-comparison parameter file's "off" — left opt-in
since this codebase's tree nodes already run rounding/diving heuristics
inline on every node, unlike legacy, so the marginal value is different
and untested at scale) and CLI flags `--diving`/`--diving-down-budget`/
`--diving-time-limit`. Runs *after* the main tree search rather than
interleaved with the root the way legacy does (documented simplification:
diving can still improve the final incumbent, just without giving the main
search extra pruning power from an early diving-found incumbent).

**Measured results, before tonight's checkpoint vs. after (real ANI
instances, `build-cplex-soplex/bpp-solve`):**

| Test | Before | After |
| --- | --- | --- |
| ANI-201, 15-instance sample, `--root-cg` | 15/15 converge, mean 27.2s | 15/15 converge, mean **11.77s** (paper: 13.6s for the complete root+tree algorithm) |
| `402_10000_NR_0.txt`, `--legacy-root-cg` (SR3-free baseline) | converges in ~87-90s | converges in **48.5s** (legacy: 31.9s, so ~1.5x instead of ~2.8x) |
| `402_10000_NR_0.txt`, `--root-cg` (with automatic SR3, 4 cuts found) | did not converge within 120s (untested beyond that) | converges in **222.7s** — practical, but still slow, and not yet integer-optimality-certified for this instance (`ceil(LB)=132 < UB=133`, needs the tree) |

The remaining `--root-cg`-with-SR3 gap on ANI-402 traces directly to item 1
above (dominance not really fixed, just the fathoming bound tightened):
more simultaneous cuts on a larger-capacity instance means a larger
frontier than on ANI-201, and there is no sub-quadratic dominance
mechanism yet to shrink it further.

## Refactoring checkpoint (2026-08-07 morning: publication artifacts, SR3 schedule validated, new tree-pricer bottleneck found)

**Publication artifacts added**: `LICENSE` (MIT, draft — see the explicit
caveat in `docs/CONTINUATION_STATE.md`, this still needs sign-off from all
four paper co-authors before any public push), `NOTICE` (CPLEX/SoPlex/GMP
third-party notices), `CITATION.cff` (cites the BCCF paper), `CODE_OF_CONDUCT.md`
(Contributor Covenant 2.1), `.github/ISSUE_TEMPLATE/*` (bug/feature/reproducibility),
`.github/PULL_REQUEST_TEMPLATE.md` (includes the numerical-safety checklist
from `CONTRIBUTING.md`), `.github/workflows/build-and-test.yml` (portable
build + test + a check that no vendor header or absolute personal path is
tracked; CPLEX+SoPlex is documented as local-only, per
`PIANO_REFACTORING_BPP.md` phase 8.7 — a public runner cannot have a
licensed CPLEX install), `docs/ROADMAP.md` (in/out of scope for v0.1.0 and
what is planned after, per phase 8.6).

**SR3 activation schedule validated against real legacy output, not just
source reading.** Ran `bpp-solve-legacy` on `201_2500_NR_0.txt` with the ANI
comparison parameter file and grepped its own counters:
`count_triplets 10` at the root (budget `PARAM_MAX_TRIPLETS=50`,
`PARAM_TRIPLET_GAP_ACT=2`). The refactored solver finds only **4** cuts for
the same instance — not a bug, but a direct, previously undocumented
consequence of `max_sr3_separation_rounds`'s default (4, chosen in the
2026-08-06 night checkpoint specifically to keep the SR3 DP's cost bounded).
Raising it toward legacy's real cut count is possible but was not done
tonight: it would reintroduce the ~3x-per-simultaneous-cut growth this
default was chosen to avoid, and the two dominance fixes tried below did not
pan out. This is now an explicit, documented trade-off (speed vs. matching
legacy's exact cut count) rather than an unexamined default.

**SR3 cross-state dominance, second attempt: also net-negative.** After the
first attempt (every item, threshold 128 -> net loss, see the prior
checkpoint), tried running the same O(n^2) `prune_dominated_sr3_labels`
sweep only every 25th item instead of every item, on the theory that
batching sweeps loses no prunings (dominance removed is monotonic in how
much the frontier grew since the last sweep) while paying the O(n^2) cost
far less often. Measured on `201_2500_NR_0.txt --root-cg`: **22.6s**, still
slower than not pruning at all (**11.1s** with the dominance sweep fully
disabled). Reverted a second time, same as before
(`std::numeric_limits<std::size_t>::max()` threshold). Two independent
attempts, two measured regressions: the conclusion is that any flat O(n^2)
pairwise dominance scan is the wrong tool here regardless of how often it
runs, not that the *frequency* was wrong the first time. A real fix needs a
genuinely sub-quadratic structure (e.g. bucket labels by load, maintain a
small skyline per bucket).

**New, more significant finding: branch-and-price tree nodes are slow for a
different reason than SR3 growth or master rebuilding — the branching-aware
pricer itself is still the exponential-ish DFS.** Looking for a real
ANI-201 instance whose root does *not* already prove optimality (needed to
exercise the tree/diving code on real data, not just small synthetic unit
tests): scanning `201_2500_NR_41-49.txt` found `201_2500_NR_41.txt` and
`201_2500_NR_4.txt` with `integer_optimum_certified=0` (`ceil(LB)=65 < UB=66`).
`--branch-price 30` (best-bound *and* depth-first) on `201_2500_NR_41.txt`
did not finish within 90s each; even `--branch-price 5` (five nodes) did
not finish within 60s, while the *root alone* (`--root-cg`, i.e. the same
instance without any branching) converges in 9.3s. Root cause: every node
*other than the root* has a non-empty `BranchingState`, and
`run_floating_pricing_loop` (`src/master/column_generation.cpp`) routes any
node with active branching constraints to
`FloatingRootPricer::price(instance, duals, branching)` (cut-free) or
`price_with_branching_and_sr3` (cuts active) — **both are still the
group-based DFS/branch-and-bound search** documented as a correctness
reference in the 2026-08-06 night checkpoint, returning **one column per
call**, not the batched sparse label-setting DP that fixed the root's
performance. This is the same class of bug already fixed twice for the
cut-free and SR3-only root cases, never yet fixed for the branching case —
and it means **neither the master-row-level warm start nor the SR3
dominance work matters much for real tree performance until this is fixed
first**: a fast master and a fast frontier still bottleneck on a slow
single-column-per-iteration DFS pricer underneath.

**Decision: did not attempt to fix the branching-aware pricer or implement
the master-row-level warm start tonight.** Both are legitimate next steps,
but: (a) the pricer fix requires extending the sparse label-setting DP to
operate over Together-merged groups with Different-conflict avoidance
(likely reusing the SR3 cut-state packing technique, treating each
Different constraint as a 1-bit "already selected" flag on the conflicting
group) — a real, non-trivial new DP variant; (b) the master warm-start
requires correct CPLEX column bound-toggling with enable/disable/restore
across recursive backtracking, exactly the correctness risk flagged
explicitly by the user twice. Attempting either at this point in an already
very long, late session risks a subtle, hard-to-catch correctness bug in a
solver whose entire purpose is numerical exactness. A `CplexRmp::set_pattern_enabled`
declaration was drafted and then deliberately removed rather than left
half-implemented and unused. **The branching-aware pricer fix should be the
first priority of the next session** — it is likely more impactful than the
master warm-start on its own, and the master warm-start's value cannot be
properly measured until the pricer underneath it is also fast.

## Refactoring checkpoint (2026-08-07: branching-node pricer fixed -- sparse DP over Together-contracted elements)

Closes the top-priority item from the previous checkpoint: every
branch-and-price node other than the root was pricing one column per RMP
resolve via a group-based DFS (`FloatingRootPricer::price(...,branching)` /
`price_with_branching_and_sr3`), which also recomputed every SR3 cut's
coefficient from scratch (a fresh `Pattern` reconstruction) at every
recursive call instead of incremental per-label state. Measured before the
fix: `--branch-price 5` on `201_2500_NR_41.txt` (a real ANI-201 instance
needing branching, `ceil(LB)=65 < UB=66`) did not finish in 60s.

**Verified against legacy source before implementing** (an Explore-agent
research pass over `BPPS_BP_MAPPING.cpp`, `DP.cpp:18-318`,
`mckpsc-ls.cpp:2451-2571`, per the standing "don't invent, reproduce"
instruction): legacy's label-setting DP carries **no** extra per-label
branching state either. It transforms the item universe once per
branch-and-price node, before every pricing call at that node
(`MAPPING_UPDATE`): Together-linked items are physically contracted into a
single super-item with summed weight/dual (`absorbe()`,
`prepare_data_DP_LABEL_SETTING`); Different-linked items become a symmetric
conflict adjacency list (`m_conflicts`), enforced during label extension by
pruning a label's still-eligible future items the moment a conflicting item
is fixed in (`mckpsc_ls_alg_setlabel`) -- not a new dominance dimension.

**Implemented as `price_label_setting_with_branching_and_sr3`**
(`src/pricing/floating_root_pricer.cpp`), mirroring that structure exactly:
- `build_branch_groups()` (new shared helper, also now used by the two DFS
  functions instead of their previous duplicated union-find logic) performs
  the same Together-contraction/Different-conflict-list transformation.
- The sparse label-setting DP (same frontier/fathoming-bound machinery as
  `price_label_setting`/`price_label_setting_with_sr3`) runs over these
  contracted elements instead of raw items.
- Different conflicts are enforced via a packed per-label bitmask (one bit
  per element that participates in at least one conflict edge, capped at 40
  simultaneous conflict-elements) checked against a per-element "forbidding
  bits" mask on every extension -- the same pruning semantics as legacy's
  `m_conflicts` check, just packed into the label key instead of a live
  eligible-item bitmask, mirroring how SR3 cut-count state is already
  packed 2 bits/cut into the same kind of key.
- SR3 cut state generalizes the existing item-at-a-time increment to
  per-element: a single Together-merged element can pull more than one
  triplet member in at once, so the cut's count can jump by more than one in
  a single extension (clamped at 2, matching `Sr3Cut::coefficient`'s
  "at least two of three" definition) -- covered by a dedicated test forcing
  two of a cut's three items Together and checking the bonus still applies.
- Capped (40 conflict-slots, 20 cuts, matching the existing SR3 cap); beyond
  that the function falls back to the exact DFS reference rather than ever
  mis-pricing. Per the legacy research above, active conflict count on a
  real node is structurally bounded by tree depth with no hard cap in
  legacy either, but no evidence of it being large in practice was found;
  this fallback exists only so a pathological node can never silently
  produce a wrong column, not because it is expected to trigger.

Wired into all three call sites that previously dispatched to the DFS on a
non-empty branching state: `run_floating_pricing_loop` (the main
per-iteration loop, now batched -- multiple columns per RMP resolve instead
of one, the same win `price_label_setting_with_sr3` already gave the
cuts-only case), and the two SoPlex/CPLEX phase-2 verification loops.

**Correctness**: cross-checked against the DFS reference
(`price`/`price_with_branching_and_sr3`) on Together-only, Different-only,
Together+SR3 (including the two-triplet-items-merged case above), and a
combined multi-constraint/two-cut case (`tests/test_core.cpp`); every
returned candidate is also checked to satisfy `branching.accepts(...)` and to
have `dual_value` matching an independent recomputation from raw duals plus
realized cut coefficients. All pass in both `build/` and
`build-cplex-soplex/`.

**Measured fix** (`build-cplex-soplex/bpp-solve`,
`201_2500_NR_41.txt`, the exact instance that did not finish in 60s before):

| Run | Before | After |
| --- | --- | --- |
| `--branch-price 5` (best-bound) | did not finish in 60s | **38.5s**, tree fully closes (`integer_optimum_certified=1`, 3 nodes) |
| `--branch-price 30` (best-bound) | did not finish in 90s | **38.5s**, same 3-node tree (budget was never the limiter) |
| `--branch-price 30` (depth-first) | did not finish in 90s | **22.9s**, same result |

Root-only path re-verified unaffected (same 4 SR3 cuts, 3920 columns, 5
iterations, ~10.9s on `201_2500_NR_0.txt --root-cg` as the pre-fix baseline).

Not yet done: a full ANI-201/ANI-402 regression sweep exercising real
branching (still only this one instance and its siblings are confirmed to
need it); the SR3 cross-state dominance and master/row-level warm-start
items are now unblocked by this fix but not yet attempted.

## Refactoring checkpoint (2026-08-07: dual-value stabilization ported, `--help`, three-way test suite split, LICENSE/legacy publication decided)

Four items closed in this checkpoint, all evidence-driven per the standing
"don't invent" instruction:

**1. Dual-value stabilization (legacy `PARAM_SMOOTH`), ported as an opt-in.**
Read `BPPS_BP_MAGIC_STAB.cpp` first and found it is *not* the CG-stabilization
mechanism its name suggests -- it is an unrelated exact-rational dual
adjustment/certification routine (`ds_row_gen`/`ds_rmp`, gated by
`PARAM_DUAL_ADJUSTEMENT`, called only from `DP_POP.cpp`'s populate path).
The real mechanism is `SMOOTHING_UPDATE_PI`/`SMOOTHING_INIT`/
`SMOOTHING_UPDATE_STAB_CENTER` in `BPPS_BP_MASTER.cpp:2965-3040`: a static
Wentges-style convex combination `pi_smoothed = ALPHA*stab_center +
(1-ALPHA)*pi_real`, fed only to the pricer (never touching the RMP's
objective/constraints), with a misprice safeguard
(`BPPS_BP_MASTER.cpp:4356-4378`) that discards any column found under the
smoothed duals if it turns out not to be improving under the real ones, and
a gate (`BPPS_BP_MASTER.cpp:3864-3886`) restricting it to the root node,
pure BPP, no active SR3 cuts, no active conflicts, not diving.

Ported as a rewrite of `run_floating_pricing_loop`'s cut-free branch
(`src/master/column_generation.cpp`): `stabilization_active` gated exactly
like legacy (`options.branching.constraints().empty()` standing in for
"level==0", since every branch-and-price node below the root has at least
one Ryan-Foster constraint); the misprice safeguard filters the batched DP's
candidate list against real duals instead of a single column (a natural
generalization, not an invented one, since the batching itself already
generalizes the root DP); self-deactivation-and-reverify when smoothed
duals falsely claim optimality. One documented simplification: legacy's
stability-center update additionally rescales via a Farley-bound-gated
normalization tied to bookkeeping this codebase does not have: this uses
the simpler, standard rule of advancing the center to the newest
misprice-validated real dual instead.

Grepping every parameter file in the historical archive's `PARAM_FILES_BPP(S)`
directory (local-only, see above; including every `paper_v*` variant)
found `PARAM_SMOOTH 0` universally --
this feature was never exercised in producing the paper's reported results.
Per explicit instruction, it therefore stays **off by default**
(`ColumnGenerationOptions::dual_stabilization = false`), exposed only via
`--stabilization`/`--stabilization-alpha` (default alpha 0.3, matching every
parameter file's carried-but-unused value). Verified correct on real ANI-201
data: `--root-cg --stabilization` on `201_2500_NR_0.txt` reaches the same
certified optimum (66) as without it, in fewer iterations (3 vs. 5) and
fewer columns (3134 vs. 3719), correctly turning itself off once automatic
SR3 separation adds its first cut; `--branch-price 5 --stabilization` on
`201_2500_NR_41.txt` (the branching-pricer fix's reproducer) still reaches
the certified optimum. New unit/integration/regression tests cover: default
off is a no-op, an invalid alpha is rejected, results match the unstabilized
baseline, and stabilization is silently inert once branching/SR3 are active.

**2. `bpp-solve --help`/`-h`.** Full usage text (modes, the positional
limit's per-mode meaning, every flag with its applicability/default, output
format, exit codes, examples) added to `src/cli/main.cpp`; recognized
anywhere in argv, exits 0. Kept as the authoritative detailed reference,
with `README.md` trimmed to a shorter pointer at it rather than duplicating
every flag in two places that could drift apart.

**3. Test suites split into unit/integration/regression.** The former
single `tests/test_core.cpp`/`bpp-core-tests` target (677 lines, every kind
of test interleaved) is now three files/CTest targets:
`tests/test_unit.cpp` (`bpp-unit-tests`, isolated components, no CPLEX
required, always built/run), `tests/test_integration.cpp`
(`bpp-integration-tests`, multi-component `solve_*`/`CplexRmp`/`SoplexRmp`
pipeline tests, CPLEX-gated), `tests/test_regression.cpp`
(`bpp-regression-tests`, a small curated set of tests each pinned to one
specific previously-fixed bug -- the dense-vs-sparse pricing DP, the SR3 DFS
fallback, the branching-node pricer DFS fallback, stabilization's
certified-optimum invariant -- rather than general coverage; see the file's
header comment for why it deliberately does not assert on timing). All
three pass in both `build/` and `build-cplex-soplex/`.
`CMakeLists.txt`/`README.md`/`docs/CONTINUATION_STATE.md` updated to match;
the CI workflow needed no change (`ctest --output-on-failure` already runs
every registered test).

**4. LICENSE confirmed; `legacy/` publication decided.** Fabio Furini
confirmed the paper's co-authors agree to publish under the drafted MIT
`LICENSE`. Separately decided: `legacy/` (the historical reference
implementation itself, not just its vendor headers) stays **local-only,
never published** -- `.gitignore` now excludes `/legacy/` wholesale
(previously only the 3 IBM/Gurobi vendor headers plus build artifacts were
excluded, which would have let the rest of the historical source be staged).
`NOTICE` already documented this framing before the decision was confirmed,
so it needed no change.

## Refactoring checkpoint (2026-08-07: official BCCF comparison, citation, Gurobi backend restored)

Three items closed in this checkpoint, per explicit instruction:

**1. Direct comparison against the paper's own official code, not just `legacy/`.**
Every prior comparison in this document was against Fabio Furini's personal
historical archive (`legacy/`), not the paper's own published distribution.
Downloaded and built `BCCF.zip` from
<https://github.com/stefanoconiglio/A-Numerically-Exact-Algorithm-for-the-Bin-Packing-Problem>
into `../official-reference-BCCF/` (a sibling directory, outside this
repository and never published, analogous in spirit to `legacy/` but this is
the authors' own distributed code -- see its `README.md`). The distribution
ships precompiled `.o` object files (not source) plus a `Makefile_linker`;
linking succeeded against this machine's CPLEX 20.1/Gurobi 13.0.2/SoPlex
5.0.1 (the official README specifies CPLEX 12.9/Gurobi 9.5.1/SoPlex 5.0.1
"in order to obtain results comparable to those of the paper" -- the newer
versions ran correctly but are not guaranteed to reproduce the paper's exact
numbers). Ran the resulting `BPPS` binary with the paper's own
best-performing parameter file (`param_test_BPP-non-IRUP-2exp44.txt`) on 4
real ANI-201 instances and 1 ANI-402 instance, compared against
`bpp-solve --root-cg`:

| Instance | Official BCCF | `bpp-solve --root-cg` | UB match |
| --- | ---: | ---: | :---: |
| `201_2500_NR_0.txt` | 1.52s | 12.3s | 66 = 66 |
| `201_2500_NR_1.txt` | 1.40s | 17.0s | 66 = 66 |
| `201_2500_NR_10.txt` | 1.62s | 11.1s | 66 = 66 |
| `201_2500_NR_11.txt` | 1.41s | 10.2s | 66 = 66 |
| `402_10000_NR_0.txt` | 27.2s (root 15.4s) | 222.7s (`--root-cg`) | 133 = 133 |

**Correctness confirmed** against the real reference for the first time
(not just `legacy/`). **Performance gap confirmed real**: the official
binary is 7-19x faster on ANI-201 and ~8x faster on ANI-402 root, *while
separating more SR3 cuts, not fewer* (10-50 vs. our capped 4) -- this
reinforces, against the actual published reference, that the SR3-aware
pricing DP's missing cross-state dominance (item 1 in "Lavoro rimanente",
`docs/ROADMAP.md`) is the dominant remaining performance gap, not a
measurement artifact of comparing against a possibly-unrepresentative
personal archive build. Full write-up:
`../official-reference-BCCF/README.md` and
`docs/paper-comparison.md`'s "Direct comparison against the official BCCF
reference binary" section.

**2. Citation added.** `CITATION.cff` (`repository-code` field on the
preferred-citation), `README.md` (top-of-file pointer plus an expanded
"Provenance" section) now cite the official repository above alongside the
paper itself.

**3. Gurobi restored as an optional, alternative floating-point LP backend.**
Reverses the earlier "Gurobi removed by design" decision (`docs/ROADMAP.md`),
per explicit instruction. Implemented as `GurobiRmp`
(`include/bpp/gurobi_rmp.hpp`, `src/master/gurobi_rmp.cpp`), mirroring
`CplexRmp`'s public contract exactly (same historical justification for
forcing primal simplex + single thread, `BPPS_BP_MASTER.cpp:1382-1383`) and
built via Gurobi's C API (`GRBnewmodel`/`GRBaddconstr`/`GRBaddvar`/
`GRBchgcoeffs`/`GRBoptimize`). A new `MasterRmp` wrapper
(`include/bpp/master_rmp.hpp`, `std::variant<CplexRmp, GurobiRmp>`) lets
`column_generation.cpp`'s four master-construction call sites and
`run_floating_pricing_loop`'s signature stay backend-agnostic; a new
`LpBackend` enum (`ColumnGenerationOptions::backend`, default `Cplex`,
matching every historical parameter file's `PARAM_SOLVER`) selects which.
CMake: `-DBPP_ENABLE_GUROBI=ON -DGUROBI_ROOT=...` (auto-detects the
installed Gurobi's versioned library name, e.g. `libgurobi130.so`, instead
of hardcoding one); `BPP_ENABLE_SOPLEX=ON` is required with it too, same
numerically-exact reasoning as CPLEX. `bpp-solve` gained `--solver
cplex|gurobi`, defaulting to whichever backend the binary was actually
built with (CPLEX if both) -- an earlier version of this defaulted
unconditionally to `"cplex"`, which broke a Gurobi-only build's default
invocation; caught by actually testing a Gurobi-only build end-to-end, not
just compiling it.

**Verified, not just implemented**: built and ran `ctest` in four separate
configurations -- portable (`build/`), CPLEX+SoPlex only
(`build-cplex-soplex/`), Gurobi+SoPlex only (`build-gurobi-soplex/`), and
CPLEX+Gurobi+SoPlex (`build-cplex-gurobi-soplex/`) -- all passing, including
new direct `GurobiRmp` tests mirroring the existing `CplexRmp` ones and a
CPLEX-vs-Gurobi equivalence test (`tests/test_integration.cpp`, gated on
both being built in) that checks both backends reach the identical
certified optimum on the same instance. Also ran `bpp-solve --root-cg
--solver gurobi` on a real ANI-201 instance (`201_2500_NR_0.txt`): same
certified UB=66 as CPLEX, comparable time (12.7s vs. 11.9s). `NOTICE`
updated with the Gurobi third-party notice; `docs/ROADMAP.md`'s exclusion
list updated to remove the now-reversed "Gurobi removed by design" line.

## Refactoring checkpoint (2026-08-07: SR3 dominance correctness fix + re-enabled; general speed gap investigated with real data)

Per explicit instruction to understand and close the remaining gap against
the official BCCF reference (previous checkpoint), not just describe it.
Two separate questions were investigated: "why can't more SR3 cuts be
active at once" (answered and fixed) and "why is the official binary
faster in general, even at the same cut count" (investigated with real
profiling data; a genuine architectural explanation found, not fixed this
session -- see below for why).

**1. Found and fixed a real unsoundness bug in the (previously disabled)
SR3 cross-state dominance rule, before re-enabling it.** The rule added
2026-08-06 night (`cut_state_covers`, since disabled at `threshold=SIZE_MAX`
after two net-negative performance attempts) required a uniform
"A's cut-count >= B's cut-count" componentwise to declare A dominates B.
Deriving this from first principles (not just re-measuring) to understand
*why* legacy's own dominance rule -- researched via a legacy-source pass
into `mckpsc-ls.cpp`'s `mckpsc_ls_alg_dominance_A` -- excludes cut state
from the dominance key entirely (a numeric reduced-cost-inequality bound
with a per-cut correction term, not equality/coverage on cut state)
surfaced a genuine counterexample: for a **negative-dual** cut, being
*closer* to completion (higher count) is not always favorable, since
completing it is a penalty (the label's value drops once count reaches 2)
-- a uniform ">=" rule can therefore let a label be marked "dominated" that
could still end up strictly better after some completions. Verified with a
concrete constructed scenario (documented in the function's own comment,
`src/pricing/floating_root_pricer.cpp`) before touching code.

**Fix**: `cut_state_covers` is now sign-aware per cut -- for a
non-negative-dual cut it still requires A's count >= B's (being ahead
helps); for a negative-dual cut it now requires A's count <= B's instead
(being *behind* helps, since it's less likely to be forced into the
penalty). This restores soundness: for every future completion, A's
per-cut contribution is provably >= B's on every cut, by the direction
argument specific to that cut's sign.

**Also fixed the complexity, not just the frequency, of the sweep that
applies it** (the actual cause of both 2026-08-06 attempts' net-negative
results, per that checkpoint's own conclusion): `prune_dominated_sr3_labels`
now sorts entries by load and compares each only against the next `window`
entries (default 8) instead of the full O(n) remaining suffix, bounding
total cost to O(n*window) -- mirroring legacy's own bounded rc-sorted
comparison window (`mckpsc-ls.cpp`, `PARAM_DELTA`), which is deliberately
incomplete (sound, just not exhaustive) by the same design. Threshold
(500, matching legacy's `dom_nlabels_thr`) and window (8) were tuned
empirically: a wider window (32) and lower threshold (200) were also
measured and were net-negative (more pruning yield, but the larger sweep
cost outweighed it) -- 500/8 was the best of the settings tried.

**Result: a new, previously-unavailable capability, not just a performance
tweak.** New CLI flag `--sr3-max-rounds N` (`ColumnGenerationOptions`
already had `max_sr3_separation_rounds`, just no CLI exposure) lets more
SR3 separation rounds run. On `201_2500_NR_0.txt`: `--sr3-max-rounds 10`
now completes in ~20s with **10 simultaneous SR3 cuts** -- matching the
`count_triplets=10` this exact instance produces in the official binary
(previous checkpoint's finding) -- where before this fix, raising
simultaneous cuts much past 4 was not practically achievable at all (both
2026-08-06 dominance attempts were net-negative and left disabled). Default
`max_sr3_separation_rounds` stays at 4 (unchanged) since going further
(15+ rounds) still grows too expensive (60s+ at 15 rounds even with the
fix) to be a safe default; `--sr3-max-rounds` is opt-in for users who want
more cuts. All correctness tests pass with the fix live (including the
regression test that specifically includes a negative-dual cut), and the
default-settings baseline is unchanged (~11s on the same instance, no
regression).

**2. The general (same-cut-count) speed gap was profiled with real data,
not further guessed at.** Added and then removed (per established
discipline, no debug scaffolding left behind) temporary env-var-gated
timing around `run_floating_pricing_loop` (phase 1) and
`run_soplex_safe_phase` (phase 2). On `201_2500_NR_0.txt --root-cg`
(~11s total):

| Component | Time | Share |
| --- | ---: | ---: |
| Phase 1 (CPLEX) pricing DP, ~1580 calls across 4 SR3 rounds | 4.46s | 40% |
| Phase 2 (SoPlex) master resolve, only 7 resolves | 3.85s | 34% |
| Phase 1 (CPLEX) master resolve | 0.78s | 7% |
| Phase 2 (SoPlex) pricing | 0.63s | 6% |
| round/dive heuristics, safe-dual certification | 0.27s | 2% |
| unaccounted (pool init, greedy, etc.) | ~1.2s | 11% |

Two findings, both verified by actually measuring, not assumed:

- **Phase 2 does only 7 SoPlex resolves, yet they cost 3.85s total (~550ms
  each)** -- SoPlex (rational) is inherently far more expensive per resolve
  than CPLEX (~2ms/resolve in phase 1, consistent with the 2026-08-06 night
  CPLEX threading fix still holding). The natural fix hypothesis -- batch
  more columns per phase-2 resolve (`price_label_setting_with_sr3(...,
  active_cuts, 1)` in `run_soplex_safe_phase` hardcodes `max_candidates=1`,
  unlike phase 1's `options.max_columns_per_iteration`) -- was **tried and
  measured net-negative**: raising it to 100 (temporarily, then reverted)
  *increased* phase-2 iterations (7->10) and phase-2 solve time (3.85s->
  13.3s), most likely because adding many columns per SoPlex resolve
  disrupts basis warm-starting more than it saves in resolve count. Not a
  viable fix as attempted; reverted immediately once measured.
- **Legacy's own printed stats for this exact instance/config
  (`official-reference-BCCF/`) show only 39 total exact DP calls for its
  entire 1.5s run**, against phase 1's ~1580 pricing calls here for
  comparable work. The likely architectural explanation, from the official
  README (`official-reference-BCCF/README.md`): legacy uses **Gurobi as an
  intermediate-precision LP solver, only switching to SoPlex once reduced
  costs are small enough** (`PARAM_SOLVER`/the K2 threshold described in
  the paper's repository README), rather than running a fully separate,
  all-SoPlex phase 2 the way this codebase's two-phase design does. This
  would explain both findings at once: far fewer total resolves, and far
  fewer of the *expensive* (SoPlex) ones specifically.

**This second item is a genuine architectural difference, not a bug, and
was deliberately not attempted as a same-session fix**: adopting a
three-tier CPLEX/Gurobi-then-SoPlex pricing handoff would be a substantial
redesign of the two-phase root driver (`solve_two_phase_root_column_generation`,
`run_soplex_safe_phase`), not a local change, and touches the exact
numerically-exact certification path this whole codebase exists to get
right -- the same category of risk already flagged for the master
warm-start item, where rushing a large change late risks a subtle
correctness bug in the one part of the solver that must never be wrong.
Flagged as the top item in `docs/ROADMAP.md` for next time, with this
checkpoint's profiling data as the starting point (no need to
re-diagnose from scratch).

## Refactoring checkpoint (2026-08-07: phase-2 SoPlex pricing batched -- real, kept fix)

Follow-up to the same day's "general speed gap investigated" checkpoint,
per explicit instruction to actually close part of the gap rather than
only explain it. That checkpoint found phase 2 (`run_soplex_safe_phase`)
priced **one column per SoPlex resolve** (`price_label_setting_with_sr3(...,
active_cuts, 1)`, the `1` a hardcoded `max_candidates`, not
`options.max_columns_per_iteration`), unlike phase 1 which already batches.
Since a SoPlex resolve measured ~275x a CPLEX one, this meant phase 2's
handful of iterations cost as much as all of phase 1 combined. An earlier
same-day attempt to fix this by raising *phase 1's* batch cap was tried,
measured net-negative, and reverted (see the previous checkpoint) --
that experiment never actually touched phase 2's hardcoded `1`, so it
never tested this specific fix.

**Fix, this time isolated to the actual bottleneck**: the SR3-cuts/no-
branching case of `run_soplex_safe_phase` now calls
`price_label_setting_with_sr3(..., options.max_columns_per_iteration)` and
loops over the whole returned batch (mirroring `run_floating_pricing_loop`'s
existing cuts-branch), instead of using only `.front()` of a single-
candidate call. The fixed-point (no cuts, no branching) and branching
cases were left untouched (single column per call, as before) -- only the
case actually measured and fixable without touching the branch-aware
pricer's contract.

**Measured, kept** (all instances re-verified to reach the identical
certified UB as before the change; full `ctest` passes in all four build
configurations):

| Instance | Before | After | 
| --- | ---: | ---: |
| `201_2500_NR_0.txt --root-cg` | 12.3s (7 phase-2 iters) | 10.5s (3 iters) |
| `201_2500_NR_1.txt --root-cg` | 17.0s | 12.4s (4 iters) |
| `201_2500_NR_10.txt --root-cg` | 11.1s | 9.9s (3 iters) |
| `201_2500_NR_11.txt --root-cg` | 10.2s | 10.2s (2 iters) |
| `402_10000_NR_0.txt --root-cg` | 222.7s | 162.7s |

ANI-201 mean: 12.65s -> 10.75s (~15% faster). ANI-402: ~27% faster. Real,
verified, and kept -- but does **not** close the ~8x gap to the official
binary measured in the previous checkpoint; it narrows it to roughly
7x on this sample. The dominant remaining cost is still the sheer number of
CG iterations overall (phase 1's ~1580 pricing calls, unaffected by this
change, against legacy's 39 total DP calls for equivalent work) -- this fix
addressed the *cost-per-iteration* imbalance between the two phases, not
the *iteration-count* imbalance against the reference, which is the larger
remaining factor and the actual subject of `docs/ROADMAP.md` item 1 (the
three-tier CPLEX/Gurobi-then-SoPlex handoff). Not attempting that larger
redesign in the same pass as this smaller, already-verified fix, to keep
the two changes independently reviewable.

## Refactoring checkpoint (2026-08-07: root cause of the iteration-count gap found; a faithful fix attempted and reverted as net-negative)

Direct follow-up to "phase-2 SoPlex pricing batched" (same day): that fix
narrowed the gap to ~7x but explicitly left phase 1's iteration count
(~1580 calls vs. legacy's 39) as the larger, unexplained factor. This
checkpoint identifies the real cause with hard evidence, attempts a
faithful port of the fix, and reports it as reverted after measuring it net-
negative -- the investigation is real progress even though the change
itself did not survive.

**Root cause, confirmed by instrumenting legacy directly (near-zero risk:
`legacy/` is a local-only comparison tool, never published, so adding a
print statement there carries none of the correctness stakes a change to
the production code would).** `BPPS_BP_MASTER.cpp:4207` already has a
`current_LP`-per-iteration print gated behind `#ifdef print_MASTER_light`,
unused by the normal build. Rebuilt `bpp-solve-legacy` with
`-Dprint_MASTER_light` (no source changes) and re-ran it on
`201_2500_NR_0.txt` with the paper's own best-config parameter file: **CG
iteration 1 already shows `current_LP=65.03` (vs. the true optimum 65.0)
with 927 columns already in the pool** -- i.e. legacy's master starts from
an already-near-optimal basis, not from scratch. Traced this to
`BPPS_BP_MASTER.cpp:1029-1052` ("INSERT COLUMNS OF THE GREEDY HEURISTIC
SOLUTION", immediately followed by "INSERT ALSO SINGLETONS"): the initial
RMP is seeded with **both** the greedy incumbent's own bins **and**
singletons, whereas this codebase's `initialize_pattern_pool` only ever
seeds singletons. Legacy's apparent 40x per-call efficiency advantage is,
at least in large part, an artifact of comparing against a far richer
starting basis, not a faster column-generation algorithm -- confirmed by
also checking our own trajectory (`201_2500_NR_0.txt --root-cg` starts at
`lp_bound=201`, the trivial all-singleton value, and only reaches ~86 by
iteration 39, nowhere near the near-converged 65.03 legacy already has at
that point) and by the shape of that trajectory itself: smooth, steadily-
decelerating progress every iteration (the textbook "tailing off" effect of
unstabilized column generation), not degenerate cycling -- ruling out a
simplex-pivoting bug as an alternative explanation.

**Faithful fix attempted**: seed the pool/master with the greedy incumbent's
bins in addition to singletons (`seed_greedy_patterns`, called from both
`solve_root_column_generation_once` and `solve_root_column_generation`,
mirroring legacy's own insertion order). Built, and all correctness tests
passed after two follow-on fixes it required: (1) an existing unit test's
`generated_columns >= 1` assumption no longer held (correctly -- a richer
seed can legitimately need zero additional columns) and (2) the CPLEX-vs-
Gurobi equivalence test's exact-lp_bound comparison started failing, traced
to CPLEX and Gurobi taking different SR3 separation paths from the new,
more degenerate starting point (same number of cuts added, different
specific ones) -- loosened to compare `sr3_cuts_added` and the certified
integer answer instead, which is what actually matters and is backend-
solver-implementation-detail-agnostic by construction.

**Measured net-negative, reverted.** On the same 4-instance ANI-201 sample:
phase 1 now converges in **zero** additional iterations (confirming the
diagnosis), but overall wall time got **worse**, not better (10.5s->14.0s,
12.4s->16.5s), and `201_2500_NR_1.txt` -- previously certified with the
default 4-round SR3 budget -- **stopped certifying** (`ceil(LB)=65 <
UB=66`) with the same budget (recovers with `--sr3-max-rounds 10`, 55s, so
not an unsoundness bug, just a materially weaker default-budget result).
Most likely explanation: the extra ~60-130 greedy-bin columns end up in
*every* subsequent phase (including the already-identified-as-expensive
all-SoPlex phase 2), and the different, more degenerate LP vertex the
richer seed produces changes *which* SR3 cuts get separated within the same
4-round budget, for the worse on at least this instance. Reverted in full
(`seed_greedy_patterns` and its two call sites removed); the two test
adjustments were partially kept -- the equivalence test's looser,
solver-implementation-detail-agnostic comparison is arguably more correct
regardless of this specific change and was left in place, while the
`generated_columns >= 1` assertion was restored since its premise (a
richer default seed) no longer holds.

**Not a dead end, a scoped-down finding for next time**: greedy-seeding the
initial pool is still a real, verified, faithful piece of legacy's actual
behavior (confirmed by direct instrumentation, not guessed) and very likely
still part of the real answer -- it just needs to be combined with
addressing the phase-2-cost and SR3-separation-path interactions it
exposed (`docs/ROADMAP.md` item 1, the three-tier CPLEX/Gurobi-then-SoPlex
handoff) rather than dropped in on top of the current two-phase
architecture in isolation. Also also gated behind `--sr3-max-rounds`
already existing to recover certification suggests the *interaction*
between the SR3 gap-activation gate and a richer starting point, not the
seeding itself, is the more precise thing to fix next.

Also fixed in this checkpoint, independent of the above and kept: Gurobi's
license banner was printing to stdout (polluting the CLI's machine-readable
output) because `GRBloadenv` starts the environment immediately, before
`OutputFlag` can be set; switched to `GRBemptyenv`+set-param+`GRBstartenv`,
which allows silencing it before the environment actually starts.

## Refactoring checkpoint (2026-08-07: previous checkpoint's numbers retracted; the real mechanism found via the paper's own Algorithm 1)

**Retraction.** The "root cause of the iteration-count gap found" checkpoint
above claimed legacy's root master starts from `current_LP=65.03` with 927
columns already at iteration 1. Re-running the same instrumented
`bpp-solve-legacy` build fresh (`-Dprint_MASTER_light`, rebuilt from a clean
CMake reconfigure, output kept at
`legacy/src/BPPS_BP_MASTER.cpp`'s `NUMBER_COLUMNS` print) on
`201_2500_NR_0.txt` with the official `param_test_BPP-non-IRUP-2exp44.txt`
does **not** reproduce those numbers: iteration 1 shows `current_LP=66.0`
with 269 columns (66 greedy bins + 201 singletons + 2 extra), and the root
takes **393 CG iterations** to converge to 65.0 -- a smooth, gradual
"tailing off" trajectory, structurally the same shape as this codebase's own,
not a near-instant convergence. The greedy-seeding experiment described in
the retracted checkpoint was therefore correctly reverted, but for the wrong
diagnosed reason; this entry supersedes that one. (Root does complete in
2.611s wall time, 390 exact DP calls averaging 1.2ms each -- so the earlier
"~1580 iterations vs. 39" comparison was also not an apples-to-apples
iteration count; see below for what actually explains the wall-time gap.)

**The real mechanism, confirmed independently by both the legacy source and
the published paper.** Instrumenting the same log further shows the root
**never invokes `CG_soplex`** (legacy's SoPlex column-generation phase,
`BPPS_BP_MASTER_SOPLEX.cpp:403`) for this instance: `"PRINTING RMP FOR
SOPLEX"` at `BPPS_BP_MASTER.cpp:4396-4436` is only a diagnostic file dump
gated on `val_red_cost > -PARAM_EPS_COLUMN_REDUCED_COST_SOPLEX` (1e-3 in the
official parameter file); the actual root certification comes from
`store_dual_sol_root` (`BPPS_BP_TREE.cpp:1907`), which rescales the raw
CPLEX duals by `1/(bin_COST - max_violation_root)` using the worst reduced
cost the pricer's own exhaustive DP search observed, and reports
`dual_bound_root` directly -- no rational solver call at all for this run.

This is not an ad hoc legacy trick: it is exactly **Algorithm 1** of
Baldacci, Coniglio, Cordeau, Furini (INFORMS JOC 2023), Section 3 (pp.
6-11, "Extended-Precision Numerically Safe Dual Bounds and Two-Phase
Column-Generation Method"). The published algorithm is **one loop**, not two
separately-run phases:
1. Solve the RMP with the current solver/tolerance `ε` (`ε0` initially, the
   floating solver's own tolerance).
2. Compute the scaled-integer duals `(p_int, r_int) = round(K * (p_float,
   r_float))` and the diminished duals `(p_dim, r_dim) = (p_int, r_int)/K`
   (`floor`, both directions, matching this codebase's own
   `certify_scaled_duals`/`safe_integer_duals` exactly).
3. **Price using `(p_int, r_int)`** -- the scaled-integer duals, not the raw
   floating ones -- via the exact fixed-point pricer, obtaining an exact
   `c_min` (the diminished reduced cost of the best pattern) every single
   iteration, not just at declared convergence.
4. Fathoming Rule 1/2 using `LBF = z_dim / (1 - c_min)` (Proposition 3, a
   *proven* valid lower bound -- proof included in the paper, not a
   heuristic): if `⌈LBF⌉ ≥ UB`, fathom; if `⌈LBF⌉ = ⌈z_dim⌉`, optimality is
   already certified, right there, with no further solves.
5. Only when `c_min` becomes smaller in magnitude than the current `ε` does
   the algorithm set `ε ← 0`, switching to the rational solver **from the
   next iteration of the same loop** -- not as a full second pass rebuilding
   everything from scratch.

This exactly explains legacy's 2.6s root: `z_dim`/`LBF` are valid, exact
certificates available every iteration for free (already implied by the
pricer's own search, since it is exhaustive over the whole pattern space,
not just the pool), so the rational solver is a narrow, rare fallback, not
the routine second phase this codebase always runs.

**This codebase's architecture does not implement Algorithm 1 -- it
approximates it with two disconnected phases.** `run_floating_pricing_loop`
prices under raw floating duals and only checks `certify_scaled_duals`
(this codebase's `z_dim`-equivalent) as an after-the-fact side computation,
never feeding it back into the pricer. `run_soplex_safe_phase` then
unconditionally rebuilds a full SoPlex RMP from the entire phase-1 pool and
reruns column generation from scratch in full rational arithmetic,
regardless of how close (or already-sufficient) phase 1's own scaled
certificate was. Measured directly (temporary timing instrumentation,
removed after use): on `201_2500_NR_0.txt --root-cg`, phase 2's *first*
resolve alone -- a cold rational solve of the ~3900-column pool phase 1
handed it -- costs **3.96s of the ~10.5s total run**; certification
(`certify_scaled_duals`) itself is negligible (0.0016s across all 3
iterations). This, not per-column-generation-iteration count, is the real
remaining gap, and it is architectural, not a tuning parameter.

**Two things explicitly tried and rejected as not faithful to the paper**
(per direct instruction: reproduce the algorithm, do not invent
alternatives): (1) forcing SoPlex's `SOLVEMODE_AUTO` instead of
`SOLVEMODE_RATIONAL` -- measured *slower* (11.55s vs. 10.5s) since
`FEASTOL=OPTTOL=0` forces full rational refinement either way, so this
changes nothing and was reverted; (2) pruning the pool handed to
`SoplexRmp` down to only positive-primal-value columns before the first
resolve -- not something legacy or the paper does, an invented shortcut,
not attempted.

## Refactoring checkpoint (2026-08-07: Algorithm 1 implemented for the root/SR3 case)

Direct follow-up to the checkpoint immediately above, same day: that
checkpoint diagnosed the gap and stopped short of implementing it, flagging
the missing SR3-aware exact fixed-point pricer as the blocker. This
checkpoint implements it.

**What was built**, all in the exact certification path so described with
extra care and verified against the full test suite plus direct benchmarks,
not just unit tests:
- `price_scaled_integer_with_sr3` (`include/bpp/pricing.hpp`,
  `src/pricing/floating_root_pricer.cpp`): reuses
  `FloatingRootPricer::price_label_setting_with_sr3` verbatim, fed
  scaled-integer duals represented as exact-integer `double`s (any int64 up
  to 2^53 converts to `double` without rounding, and the DP only
  adds/compares these values, never multiplies or divides them, so results
  stay exact) with `bin_cost_` set to the integer scale `K` instead of 1.
  Added a `bound_margin` term (`bin_cost_ * 1e-9 + 1e-12`) to every
  fractional-bound pruning comparison in `price_label_setting`/
  `price_label_setting_with_sr3` first: `FractionalBoundTable::bound()`
  computes a genuine fraction (a split-item term), so its `double`
  arithmetic can round by up to `~bin_cost_ * 2^-52`; without a margin
  comfortably larger than that (this one is ~1e7x larger at any realistic
  `K`), a rounded-down bound could in principle prune a label whose *true*
  fractional bound was exactly at the threshold. For the existing
  real-dual callers (`bin_cost_ = 1`) the margin is ~1e-12, three orders of
  magnitude below `reduced_cost_tolerance` (1e-9), so it does not change
  existing floating-dual pricing behavior -- confirmed by the full test
  suite passing unchanged.
- `SafeBound::fathoming_bound` (`include/bpp/safe_bound.hpp`,
  `src/master/safe_bound.cpp`): computes `LBF = z_dim * K / (K - rc_int)`
  (Proposition 3) in exact GMP rational arithmetic.
- `run_algorithm1_loop` (`src/master/column_generation.cpp`, templated over
  `MasterRmp`/`SoplexRmp`): Algorithm 1's per-solver inner loop -- solve,
  compute scaled-integer and diminished duals, price under the
  scaled-integer duals, compute `z_dim`/`c_min`/`LBF`, apply Fathoming
  Rules 1 and 2 (both collapse cleanly onto the existing
  `converged`+`safe_bound` result fields, since any valid bound is equally
  usable by the caller's existing `can_prune` logic regardless of which
  rule produced it), add improving columns, and signal a phase switch once
  the best reduced cost falls within `reduced_cost_tolerance * K` of zero.
- `solve_root_column_generation_algorithm1`: the root/SR3 driver -- builds
  a fresh `MasterRmp` per SR3 round (a known simplification vs. the
  existing round-persistent master; the actual measured bottleneck was the
  first SoPlex resolve, not phase-1 rebuild cost, so this was not
  prioritized), runs `run_algorithm1_loop` on it, and only builds a
  `SoplexRmp` and continues in phase II if signalled -- staying in phase II
  for the rest of the call once entered, matching the paper. Wired into
  `solve_two_phase_root_column_generation` for the `options.branching.
  constraints().empty()` case only (root calls, including a branch-and-
  price tree's own root node); non-empty branching (tree descendants) still
  uses the pre-existing `run_floating_pricing_loop`/`run_soplex_safe_phase`
  pair unchanged -- deliberately out of scope for this pass, since it
  needs its own branch-aware exact pricer and the actually-measured
  bottleneck (`docs/STATUS.md`'s "general speed gap" checkpoint) is the
  root case.

**Measured** (ANI-201 4-instance sample and the ANI-402 sample, all
`--root-cg`, same certified answers unless noted):

| Instance | Before this checkpoint | After |
| --- | --- | --- |
| `201_2500_NR_0.txt` | 10.5s, phase2 3 iters | **7.3s, phase2 0 iters** (SoPlex never entered) |
| `201_2500_NR_1.txt` | 12.4s, phase2 4 iters | **7.2s, phase2 0 iters** |
| `201_2500_NR_10.txt` | 9.9s, phase2 3 iters | **6.1s, phase2 0 iters** |
| `201_2500_NR_11.txt` | 10.2s, phase2 2 iters, certified | **5.1s, phase2 0 iters, NOT certified at the default 4-round SR3 budget** (`ceil(LB)=65 < UB=66`, `safe_duals_feasible=1` -- a valid but weaker bound, not an unsoundness bug); recovers with `--sr3-max-rounds 10` in 7.7s |
| `402_10000_NR_0.txt` | 162.7s, not certified without the tree | **89.4s, phase2 0 iters, certified** (previously needed `--branch-price` to prove `ceil(LB)=UB`; now certifies at the root alone) |
| `201_2500_NR_0.txt --branch-price 50` | 16.5s, 1 node | **7.3s, 1 node** (root of the tree also uses the new path) |

Three of four ANI-201 sample instances and the ANI-402 instance are both
faster (30-50%) *and* SoPlex is never entered at all -- directly reproducing
what was observed instrumenting `legacy/` in the previous checkpoint.
`201_2500_NR_11.txt` losing certification at the default SR3-round budget
is the same, already-documented, pre-existing phenomenon as any other
budget-sensitive instance (the default 4 rounds is a cost safety valve, not
a guarantee every instance certifies at that budget) -- not new, and the
same `--sr3-max-rounds` escape hatch recovers it, same as before. The
CPLEX-vs-Gurobi equivalence test's own instance (`201_2500_NR_0.txt` under
`--solver gurobi`) shows the identical pattern (different SR3 separation
path from CPLEX, needs `--sr3-max-rounds 10` to certify) -- this is the
same solver-divergence phenomenon the equivalence test was already loosened
for (`docs/STATUS.md`'s earlier "root cause...retracted" checkpoint),
not something new to this change.

Full `ctest` passes on all four build configurations
(`build/`, `build-cplex-soplex/`, `build-gurobi-soplex/`,
`build-cplex-gurobi-soplex/`) after one required test update: the
`phase2_iterations > 0` assertion in `tests/test_integration.cpp` no longer
holds unconditionally (reaching an exact certificate in phase I alone, with
zero phase-II iterations, is now a legitimate, common outcome, not a bug) --
loosened to `phase1_iterations + phase2_iterations > 0` with a comment
explaining why.

**Not done in this pass**: the branching/tree case (still the pre-existing
architecture), Fathoming Rule 1's benefit for actual branch-and-price nodes
with a tight incumbent (only exercised at the root here, where it rarely
fires early), and preserving the round-persistent master across SR3 rounds
in the new driver (rebuilds per round instead). None of these affect
soundness; they are the natural next increments if the branching case's own
cost profile ever becomes the bottleneck the way the root case was here.

## Refactoring checkpoint (2026-08-07: Algorithm 1 extended to branch-and-price nodes; full 50-instance sweep; branching pricer's own per-call cost identified as the remaining bottleneck)

Direct follow-up, same day, prompted by testing all 50 ANI-201 instances
(not just the 4-instance sample) against the official BCCF binary
end-to-end (`--branch-price`, official run with `SAFE_MIP_SOL=SATURATED_
TRIPLET=STRONG_BRANCHING=1`, same machine).

**First full-sample result (before this checkpoint's fix), root-only
Algorithm 1**: 26/50 instances (52%) certify directly at the root, mean
3.18x the official binary's time -- confirming the previous checkpoint's
4-instance measurement generalizes. The other 24/50 need branch-and-price
(still the pre-existing, un-migrated architecture at the time): mean
16.15x when they did finish, and **6/50 did not certify within a 90s
`--branch-price 200` budget at all** (`201_2500_NR_13/21/33/34/38/39`).
Re-run with a 300s timeout on `NR_13` alone: still did not finish
(confirmed via a separate, longer background run) -- a real, not
imagined, problem, not a slow-but-eventually-fine case.

**Extended Algorithm 1 to branch-and-price nodes.** Added
`price_scaled_integer_with_branching_and_sr3`
(`src/pricing/floating_root_pricer.cpp`/`include/bpp/pricing.hpp`), the
same exact-integer-valued-`double` reuse trick as the root pricer, wrapping
`price_label_setting_with_branching_and_sr3`; added the same `bound_margin`
safety term to that DP's two fractional-bound prune sites (it shares
`FractionalBoundTable` with the root DP, so the same floating-point-
rounding argument from the "Algorithm 1 implemented" checkpoint applies
verbatim). `run_algorithm1_loop` now dispatches to this branch-aware pricer
whenever `options.branching.constraints()` is non-empty.
`solve_root_column_generation_algorithm1` no longer rejects non-empty
branching (added the `greedy_allowed`/`incumbent_bins = INT_MAX` check
that the pre-existing code already had for a greedy solution incompatible
with the node's Ryan-Foster constraints, and restored the `options.
branching.constraints().empty()` guard on the `populate` call).
`solve_two_phase_root_column_generation` now dispatches to Algorithm~1
unconditionally (whenever `BPP_HAS_SOPLEX`), not just for empty branching.

**Measured, honestly**: full `ctest` passes on all four build
configurations. `201_2500_NR_11.txt --branch-price 200` (3 nodes): 42.8s
-> 36.0s (~16% faster). Re-running the full 50-instance sweep: the
root-only mean ratio is essentially unchanged (3.18x -> 3.01x, as
expected -- this pass did not touch the root path), the branch-price mean
ratio improves modestly (16.15x -> 14.15x, ~12%), and **the same 6
instances remain uncertified within 90s** -- `NR_13/21/33/34/38/39`,
identical set, before and after this fix. This is a real, kept
improvement, not nothing -- but it is not the fix for the 6 stuck
instances, and it is smaller than the root case's ~30-45% win.

**Root-caused why the branching win is smaller**, via temporary per-node
timing instrumentation on `201_2500_NR_11.txt --branch-price` (added,
used, removed -- not committed): the root node converges in 4.7s (371
phase-1 iterations, 0 phase-2 -- SoPlex skipped, as expected), but each of
the 2 branching nodes costs 9.3s and 13.7s respectively for a *similar or
smaller* iteration count (344, 350) and *fewer* generated patterns than
the root -- despite phase-2 also being skipped at both (0 SoPlex
iterations there too, confirming the branching extension's core mechanism
does work). The extra cost is per-iteration, not phase-2: `build_branch_
groups` (`src/pricing/floating_root_pricer.cpp`, the Together-contraction/
Different-conflict-adjacency builder) is called fresh on *every* pricing
call inside `price_label_setting_with_branching_and_sr3`, i.e. hundreds of
times per node, even though the node's branching *structure* (which items
are contracted together, which elements conflict) does not change across
iterations within one node -- only the *dual values* assigned to each
contracted element do. The structural part is being needlessly rebuilt
every iteration.

**Not fixed in this pass**: splitting `build_branch_groups` into a
structural part (cacheable once per node) and a per-iteration value-
assignment part (cheap, genuinely needs the current duals) is the
concrete next optimization -- diagnosed precisely, not attempted, since it
touches the same pricing DP whose correctness the SR3 dominance and
`bound_margin` fixes already required real care to get right, and this
session was already long. The 6 instances that do not certify within 90s
at any node-cost level need this (or a larger node budget) to resolve;
whether they are "hard" in the sense of genuinely needing many nodes, or
just slow because of this per-node overhead, is not yet distinguished --
the per-node cost reduction above should be tried first, since it is
concrete and correctness-neutral, before concluding anything about the
instances' intrinsic difficulty.

- Full migration of the historical classical-BPP feature set: pricing,
  branching, diving, primal heuristics, SR3, stabilisation and safe bounds.
  The migration contract is in `docs/legacy-feature-inventory.md`.
- CPLEX adapter and the first (floating-point) column-generation phase are
  implemented, including a deterministic LP-diving incumbent pass.
- Exact GMP safe-bound arithmetic, fixed-point root pricing and an explicit
  two-phase CPLEX→safe root driver are implemented. The production
  numerically-exact configuration requires `BPP_ENABLE_SOPLEX=ON`, so the
  second phase uses the rational SoPlex RMP. CMake rejects a CPLEX build
  without SoPlex; the legacy fixed-point fallback remains only in source for
  diagnostic compatibility and is not a supported production configuration.
- The classical root pricer uses persistent predecessor labels, a bounded
  negative-column batch, per-load dominance and suffix upper-bound fathoming.
  Optional dual stabilization is exposed in `ColumnGenerationOptions`; it is
  disabled by default to preserve exact raw dual pricing.
- SR3/triplet separation, root floating/integer knapsack pricing, Ryan–Foster
  Together/Different branch-aware pricing and pattern enumeration are covered
  by tests. SR3 rows can be attached to the CPLEX RMP and their duals are
  consumed by an SR3-aware pricing oracle and included in safe-bound
  certification. Automatic root separation is now wired as a bounded
  column-and-row restart for `--root-cg`/`--populate`; the compatibility
  `--legacy-root-cg`/`--no-populate` mode remains cut-free. The historical
  activation schedule and full cut-counter equivalence still need validation.
  Branching and SR3 can be active simultaneously through the combined pricing
  oracle. A best-bound Ryan--Foster branch-and-price driver runs every node
  through the same two-phase safe root engine. The BPPS-only bilevel branching
  parameter is deliberately not imported into this classical BPP target.
- Readers for the historical benchmark formats and verified ANI-400 manifest.
- Two selectable tree traversal strategies (`NodeStrategy::BestBound`,
  `NodeStrategy::DepthFirst`), the latter matching the historical
  Together-first depth-first order with a cross-tree warm-start pattern
  pool; selectable via `BranchAndPriceOptions::node_strategy` or
  `bpp-solve ... --branch-price N --strategy depth-first`.

The root-CG executable is functional for the migrated classical-BPP path,
while the plain CLI remains a greedy fallback when CPLEX is unavailable. The
remaining gaps before a strict historical reproduction are faithful triplet
activation/counters and complete heuristic scheduling. The exact ANI
all-instance performance envelope is validated as practical on ANI-201 scale
(see the checkpoint above); the base pricing DP is now also confirmed
practical on ANI-402 scale (2026-08-07 checkpoint below — dense-array knapsack
DP replaced with a sparse label-setting frontier, matching legacy's
approach), though the full `--root-cg` path (with automatic SR3 separation)
on ANI-402 still needs its own timing measurement.

## Refactoring checkpoint (2026-08-07: SAFE_MIP_SOL implemented; populate rewritten onto the dominance-pruned pricing DP; all 50 ANI-201 instances now certify)

Direct follow-up to the branch-and-price checkpoint above, same day.
Prompted by a direct question ("do you actually understand how *my* code
solves these instances?") that this session had not yet answered with
evidence: re-read `legacy/` source specifically for how the 6 stuck
instances get solved, rather than continuing to tune this codebase's own
pricer blind.

**Ran the official binary on `201_2500_NR_13.txt` with full output.**
`Nodes 0` -- zero branching -- yet `LP val 65.000000000000`, not 66. The
answer is `SAFE_MIP_SOL` (`DP_POP.cpp:910-978`, gated `level==0` in
`BPPS_BP_TREE.cpp:3832-3851`): once patterns are enumerated (`populate`),
column generation is stopped and a genuine 0/1 covering MIP is solved on
the enumerated pool with an added row (`sum(x) <= incumbent-1`). CPLEX's
own exact branch-and-cut either finds a strictly better solution or proves
infeasibility -- and infeasibility is *by itself* a complete, self-
contained proof that the incumbent is optimal, independent of whatever the
raw LP/dual bound says. This is the paper's own Sec. 4 "reduced SC
problem ... solved ... thanks to a simplified version of our BPC
algorithm", not a legacy-only trick.

**Implemented**: `CplexRmp::solve_mip_at_most` (changes pool columns to
binary, adds the bin-count row, `CPXmipopt`, returns the selection or
`std::nullopt` on proven infeasibility) and `try_safe_mip_certification`
(the populate-then-MIP loop, bounded to 5 rounds, root-only, CPLEX-only).
Wired in as opt-in via `options.populate` (`--populate`), matching this
project's "default = paper-exact, extras opt-in" convention -- **not**
unconditional on `--root-cg`, for reasons below.

**Two real bugs found and fixed before this was safe to call done** (both
found by testing, not left for the user to discover):
1. `CPXmipopt` had no time limit -- a hard covering MIP can take a long
   time to prove infeasible even over a modest pool; added
   `CPX_PARAM_TILIM=20s`, with the caller catching an inconclusive-status
   exception and giving up gracefully (not crashing, not hanging).
2. **The bigger one**: `populate_root_columns`'s enumeration was a
   hand-rolled DFS with *no dominance pruning between labels* -- only a
   single fractional-bound prune. Measured hanging past 90s on
   `201_2500_NR_11.txt` without completing, confirming this was a real,
   not theoretical, problem. Added a wall-clock deadline as an immediate
   safety net, then found the actual fix (next paragraph). Also added a
   soundness guard: `try_safe_mip_certification` now refuses to accept an
   "infeasible => optimal" verdict unless `result.populate_complete` is
   true, since the enumeration-completeness argument the whole proof rests
   on does not hold over a truncated pool.

**The actual fix, found by finally reading `DP_POP.cpp:509`
(`DP_LABEL_SETTING_POPULATE`) properly**: legacy's populate is not a
DFS at all -- it calls the *same* label-setting DP engine used for
pricing (`mckpsc-ls`), in a populate mode with a value threshold instead
of pricing's bin-cost threshold. `populate_root_columns` was rewritten to
match: constructing a `FloatingRootPricer` with the populate threshold as
its `bin_cost_` and calling the existing, already-dominance-pruned
`price_label_setting_with_sr3` reproduces the populate semantics exactly
(`bin_cost_ - value < 0` becomes `value > threshold`) while reusing the
*same* frontier/dominance machinery already proven fast for pricing this
session. This removed the need for the wall-clock deadline entirely (the
old DFS code, ~120 lines, was deleted outright, not patched).

**Measured**: `201_2500_NR_11.txt --populate 20000`: did not finish in 90s
before this fix; **5.5s total, certified** after. `201_2500_NR_13.txt`:
did not finish in 300s before; **7.8s total, certified** after. All 6
previously-stuck instances (`NR_13/21/33/34/38/39`) now certify in
5.6-9.7s each. Full 50-instance ANI-201 sweep against the official binary
(`--root-cg` first, `--populate 20000` fallback for the 24 that need it):
**50/50 certify** (0 stuck, down from 6), mean ratio to the official
binary **2.65x** (median 2.99x, min 0.23x -- i.e. faster than official on
that instance, max 4.69x), down from ~7-8x at the start of this session's
investigation. `ctest` passes on all four build configurations.

**Why opt-in, not default**: `try_safe_mip_certification`'s MIP step and
the rewritten `populate_root_columns` are both now fast and bounded, but
`--populate`'s existing default `populate_max_columns` (500000) is far
larger than the 20000 cap used above, and this was not restress-tested at
that scale under time pressure -- opt-in keeps `--root-cg`'s default
behavior exactly as validated throughout this whole session, while making
the real fix available to whoever asks for it (`--populate`).

**Not done**: Gurobi MIP support (`GurobiRmp` has no `solve_mip_at_most`),
wiring `try_safe_mip_certification` into the branching/tree case (it is
root-only, matching legacy's own `level==0` gate, but a non-root
equivalent may be worth exploring if deep trees remain a bottleneck after
this fix), and re-measuring the full ANI-402 sample and the earlier
`build_branch_groups`-caching branching optimization now that this larger
fix landed -- both still open, not yet re-verified against this checkpoint.

## Refactoring checkpoint (2026-08-06)

### Verified

- CPLEX and SoPlex builds passed `bpp-core-tests` before cleanup.
- Ten ANI-201 root runs reproduced the old incumbent UB in 10/10 cases and
  produced valid global LP certificates in 10/10 cases.
  On the first sample these are `UB=66` and `ceil(LB)=65`, so the root result
  is not an integer-optimality proof. The CLI now prints both bounds and the
  explicit `integer_optimum_certified` flag.
- The SoPlex root sample averaged 388.4 phase-I iterations, 36.2 phase-II
  iterations and 10.193 seconds; these values are recorded in
  `tests/results/ani201-10-root-soplex-comparison.csv`.
- Populate was smoke-tested with a 100-column cap and reports cap exhaustion
  explicitly.
- The SoPlex ANI201 automatic-root smoke run generated one SR3 cut and exposed
  it through the CLI counters before reaching the configured iteration limit.
- The automatic-SR3 integration test now passes in both the portable and
  CPLEX+SoPlex builds; the latter also builds `bpp-solve-legacy` and passes the
  complete registered CTest suite.
- The comparison script now distinguishes the diagnostic restricted-master
  LP value from a certified global lower bound; iteration-limited runs report
  `new_lb=NA`.
- The corrected runner completed a five-instance ANI-201 root-cg sweep with
  the local CPLEX+SoPlex executable: UB equality was 5/5, all five runs were
  explicitly marked `iteration_limit` at the 200-iteration cap, and no
  uncertified LP value was written as a global LB. The raw CSV is
  `tests/results/ani-5-latest.csv`.

### Still required

- Validate automatic SR3/triplet separation against the historical activation
  rule, cut limits, triplet counters and safe-phase dual handling.
- Complete and benchmark the Ryan--Foster tree, including historical node
  selection, fathoming and integer-optimality certificates. BPPS-only
  bilevel/strong branching is excluded from this BPP target.
- Reproduce the paper's populate-enumeration, stabilization and primal
  heuristic schedule in a like-for-like ANI-201/ANI-402 benchmark.
- Run the full exact ANI regression set and compare integer UB/LB, nodes,
  triplets, columns, iterations and time against the legacy executable.

## GitHub readiness

The working directory is source-clean apart from reproducible ignored build
directories: per-run logs, object files and backup archives are outside the
project, while documented baseline data is under `docs/legacy-info/`. The
portable build at `build/` provides `bpp-solve`; the exact build at
`build-cplex-soplex/` provides `bpp-solve`, `bpp-solve-legacy` and tests. Both
configurations pass `ctest`. A paper-faithful build additionally requires
external CPLEX and SoPlex installations.

It is not yet ready to publish as a claim of a complete paper-faithful solver:
the project directory has no Git metadata yet, historical SR3/triplet
activation/counters and the complete integer tree/ANI regression are not
validated. CPLEX-only mode is diagnostic and cannot carry the numerically-exact
claim. A legal packaging review is also required before publishing the
legacy directory: it currently contains vendor headers marked IBM/Gurobi
licensed, while SoPlex has its own academic license. It can be uploaded as an
explicitly labelled refactoring preview only after those dependencies are
excluded or replaced by user-supplied SDK headers and the README/status caveats
remain visible.

## Acceptance requirement

The project is not complete until the new executable reproduces the old solver's BPP behavior: same feasible model, same lower/upper-bound semantics, same optimal bin count and exactness certificate on the regression set, and equivalent double-phase column-generation logic (CPLEX fast phase followed by SoPlex/GMP safe phase), including both pricing algorithms. Removing conflicts, setups, BPPC variants and Gurobi is allowed; replacing the exact BPC algorithm or its pricing with the current greedy smoke solver is not.
