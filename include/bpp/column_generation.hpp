#pragma once

#include "bpp/master_rmp.hpp"
#include "bpp/config.hpp"
#include "bpp/pricing.hpp"
#include "bpp/solution.hpp"
#include "bpp/safe_bound.hpp"
#include "bpp/cuts.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace bpp {

struct ColumnGenerationOptions {
  // Legacy PARAM_SOLVER: which floating-point LP backend solves the
  // restricted master. CPLEX everywhere by default, matching every
  // historical ANI-comparison parameter file; Gurobi is an alternative
  // backend, not a different algorithm (see MasterRmp's doc comment).
  LpBackend backend = LpBackend::Cplex;
  std::size_t max_iterations = 10000;
  // Legacy PARAM_MAX_COLUMNS_DP.
  std::size_t max_columns_per_iteration = 10;
  // Legacy PARAM_SCALE_FACTOR.
  std::int64_t safe_scale = 10000000000000LL;
  double reduced_cost_tolerance = 1e-9;
  bool populate = false;
  // Legacy PARAM_ENUMERATION_COLUMNS. Population stops at this many newly
  // generated columns, just like the historical label-setting driver.
  std::size_t populate_max_columns = 500000;
  // Legacy PARAM_SMOOTH: static dual-value smoothing (Wentges-style convex
  // combination of a stability center and the current LP dual, fed only to
  // the pricer, with a misprice safeguard against the real duals). Verified
  // against BPPS_BP_MASTER.cpp's SMOOTHING_* routines; only ever active at
  // the root with no SR3/branching, matching legacy's own gate exactly (see
  // run_floating_pricing_loop). Confirmed off (PARAM_SMOOTH=0) in every
  // historical parameter file found, including the paper_v* variants used
  // to produce the paper's reported results -- so this defaults to off here
  // too and must stay off in the default solve path; it exists only as an
  // explicit opt-in for testing.
  bool dual_stabilization = false;
  // Legacy PARAM_SMOOTH_ALPHA, uniformly 0.3 in every parameter file found.
  double stabilization_alpha = 0.3;
  // Historical SR3/triplet separation can be enabled for the production root
  // workflow. It is disabled for the legacy no-populate compatibility mode.
  bool automatic_sr3_separation = false;
  std::size_t max_sr3_cuts_per_round = 1;
  // The SR3-aware label-setting DP (price_label_setting_with_sr3) packs one
  // count-state per simultaneously active cut, so a single pricing call's
  // cost grows roughly 3x per additional cut active at once (measured on
  // ANI-201: ~0.1s at 1 cut, ~3.4s at 6, ~31s at 9). 4 rounds keeps every
  // call under ~0.5s by default; raise this only for small instances or once
  // the DP gets proper cross-state dominance pruning to remove that ceiling.
  std::size_t max_sr3_separation_rounds = 4;
  double sr3_tolerance = 1e-9;
  // Legacy PARAM_TRIPLET_GAP_ACT: automatic separation only runs once
  // incumbent_bins - lp_bound drops below this threshold, mirroring the
  // historical gap-based activation gate (BPPS_BP_MASTER.cpp:4705). Infinity
  // reproduces the unconditional separation used before this gate existed;
  // the historical aggressive setting (param_BPPS_BP_v1.txt) is 2.
  double sr3_gap_activation = std::numeric_limits<double>::infinity();
  // Legacy PARAM_MAX_TRIPLETS: cumulative cap on cuts separated by one call
  // (BPPS_BP_MASTER.cpp:4701/4712). Once reached, separation stops adding
  // rows for the rest of the call; the already-converged relaxation with the
  // cuts found so far is still returned, exactly like the historical code
  // simply skipping the triplet block for the remaining iterations.
  std::size_t max_sr3_cuts_total = 1000;
  // Optional Ryan-Foster node state. An empty state is the root BPP master.
  BranchingState branching;
  // Optional SR3 rows active in the restricted master and pricing problem.
  std::vector<Sr3Cut> sr3_cuts;
  // Optional externally-supplied initial columns, e.g. a pool accumulated
  // across sibling/ancestor nodes in a depth-first tree search
  // (NodeStrategy::DepthFirst). When non-empty, the pattern pool starts
  // from whichever of these patterns are feasible under `branching`
  // (checked one at a time, so an infeasible entry is simply skipped
  // rather than rejecting the whole list) instead of the default
  // singleton/branch-seed columns. This is how column generation "warm
  // starts" from history instead of rediscovering the same columns by
  // pricing at every node; the caller is responsible for making sure the
  // supplied set still spans a feasible starting basis (in practice: it
  // always contains the branch-seed patterns, because those are exactly
  // the singletons/merged-together-groups every node would otherwise seed
  // itself with, and a superset of them is always in the pool once the
  // root has been solved once).
  std::vector<std::vector<int>> warm_start_patterns;
  // try_safe_mip_certification's own certification mechanism, once the LP
  // bound alone doesn't close the gap. Default (false): a restricted,
  // no-pricing Ryan--Foster branch-and-bound over the post-populate pool:
  // it adds sum(x) <= incumbent-1 and proves that restricted problem
  // infeasible by exhaustive branching (see try_safe_mip_certification).
  // True: the earlier,
  // opt-in mechanism that hands the pool to the RMP backend's own generic
  // MIP engine (CPXmipopt/GRBoptimize) as a black box; kept available
  // since it is simpler and was measured fast, but its "infeasible"
  // verdict is only as trustworthy as that solver's own floating-point
  // branch-and-cut, not independently rationally certified.
  bool mip_certification_via_generic_solver = false;
};

struct ColumnGenerationResult {
  double lp_bound = 0.0;
  std::size_t iterations = 0;
  std::size_t generated_columns = 0;
  std::size_t phase1_iterations = 0;
  std::size_t phase2_iterations = 0;
  std::size_t populate_columns = 0;
  // Index of the first column added by the latest populate pass.  The
  // historical SAFE_MIP_SOL disables every older column before it starts
  // its fixed-pool Ryan--Foster tree (DP_POP.cpp:982-1027).
  std::size_t populate_first_pattern = 0;
  // Nodes visited by the legacy SAFE_MIP_SOL fixed-pool RF tree.  Zero means
  // the standard root certificate closed first or the tree was not reached.
  std::size_t safe_mip_nodes = 0;
  std::size_t sr3_cuts_added = 0;
  bool populate_complete = false;
  bool phase2_verified = false;
  // Backend used by the safe phase ("fixed-gmp" or "soplex").
  std::string phase2_backend = "fixed-gmp";
  bool converged = false;
  PatternPool patterns;
  std::vector<Sr3Cut> active_sr3_cuts;
  std::vector<double> master_values;
  Solution incumbent;
  int incumbent_bins = 0;
  std::optional<SafeBound> safe_bound;
  bool safe_duals_feasible = false;

  ColumnGenerationResult(const Instance& instance) : patterns(instance), incumbent(&instance) {}
};

// Runs floating-point column generation for classical BPP. When a branching
// state is supplied, initial columns and the pricing oracle honor its
// Together/Different Ryan-Foster restrictions.
ColumnGenerationResult solve_root_column_generation(
    const Instance& instance, const ColumnGenerationOptions& options = {});

// Runs the CPLEX floating phase followed by a safe scaled-dual phase. The
// second phase rebuilds the RMP from the phase-one columns and keeps pricing
// until an integer-scaled reduced-cost certificate is non-negative.
ColumnGenerationResult solve_two_phase_root_column_generation(
    const Instance& instance, const ColumnGenerationOptions& options = {});

}  // namespace bpp
