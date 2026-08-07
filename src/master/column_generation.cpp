#include "bpp/column_generation.hpp"
#include "bpp/heuristics.hpp"
#include "bpp/soplex_rmp.hpp"

#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <numeric>
#include <map>
#include <functional>
#include <climits>
#include <limits>

namespace bpp {

namespace {

std::vector<std::int64_t> safe_integer_duals(const std::vector<double>& duals,
                                             std::int64_t scale) {
  std::vector<std::int64_t> scaled;
  scaled.reserve(duals.size());
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("floating duals must be finite");
    const long double value = std::floor(std::nextafter(
        static_cast<long double>(dual), -std::numeric_limits<long double>::infinity()) *
        static_cast<long double>(scale));
    if (value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
      throw std::overflow_error("scaled dual does not fit in int64");
    }
    scaled.push_back(static_cast<std::int64_t>(value));
  }
  return scaled;
}

// Historical DP_LABEL_SETTING_POPULATE enumerates columns whose reduced cost
// is inside the incumbent/LB gap, then resolves the enlarged RMP.  The old
// implementation used a label-setting oracle with dominance disabled.  This
// bounded DFS has the same observable contract (gap filter, column cap and a
// final RMP solve) while retaining the modern PatternPool/RMP ownership.
void populate_root_columns(const Instance& instance,
                           const ColumnGenerationOptions& options,
                           ColumnGenerationResult& result) {
  result.populate_columns = 0;
  result.populate_complete = false;
  if (options.populate_max_columns == 0) {
    result.populate_complete = true;
    return;
  }

  MasterRmp master(options.backend, instance, options.sr3_cuts);
  for (const auto& pattern : result.patterns.patterns()) master.add_pattern(pattern);
  master.solve();
  result.master_values = master.primal_values();
  result.lp_bound = master.objective_value();

  const double gap = std::max(
      static_cast<double>(std::max(result.incumbent_bins - 1, 0)) - result.lp_bound, 0.0);
  // A column with reduced cost < gap is one of the columns accepted by the
  // historical populate oracle.  Keep the numerical tolerance one-sided.
  const double threshold = 1.0 - gap - options.reduced_cost_tolerance;
  const auto& item_duals = master.duals();
  const auto& cut_duals = master.sr3_duals();

  // Legacy's DP_LABEL_SETTING_POPULATE (DP_POP.cpp:509) does NOT do a plain
  // combinatorial DFS: it calls the *same* label-setting DP engine used for
  // pricing (mckpsc-ls), just in "populate" mode with a value threshold
  // instead of pricing's implicit bin_cost_ threshold. A from-scratch DFS
  // (this function's previous implementation) has no dominance pruning
  // between labels at all, unlike the pricing DP's frontier -- it was
  // measured to run past 90s on a hard ANI-201 instance without completing
  // (see docs/STATUS.md). Constructing FloatingRootPricer with the
  // populate threshold as its "bin_cost_" reuses price_label_setting_
  // with_sr3's dominance-pruned frontier verbatim: its accept condition
  // (`bin_cost_ - value < 0`, i.e. value > bin_cost_) becomes exactly
  // "value > threshold", and its own fractional-bound pruning tightens to
  // the same threshold automatically.
  std::vector<Sr3Cut> cuts_with_duals = options.sr3_cuts;
  for (std::size_t cut = 0; cut < cuts_with_duals.size(); ++cut) {
    cuts_with_duals[cut].dual = cut_duals[cut];
  }
  const FloatingRootPricer populate_pricer(threshold);
  const auto candidates = populate_pricer.price_label_setting_with_sr3(
      instance, item_duals, cuts_with_duals, options.populate_max_columns);
  bool hit_limit = candidates.size() >= options.populate_max_columns;
  for (const auto& candidate : candidates) {
    if (!candidate.pattern.has_value()) continue;
    const auto before = result.patterns.size();
    result.patterns.add(candidate.pattern->items());
    if (result.patterns.size() == before) continue;
    master.add_pattern(result.patterns.at(result.patterns.size() - 1));
    ++result.populate_columns;
    ++result.generated_columns;
  }
  if (!hit_limit) result.populate_complete = true;
  if (result.populate_columns > 0) {
    master.solve();
    result.master_values = master.primal_values();
    result.lp_bound = master.objective_value();
    const auto rounded = round_master_solution(instance, result.patterns.patterns(),
                                                result.master_values);
    if (rounded.bin_count() < result.incumbent_bins) {
      result.incumbent = rounded;
      result.incumbent_bins = rounded.bin_count();
    }
    const auto dived = dive_master_solution(instance, result.patterns.patterns(),
                                             result.master_values);
    if (dived.bin_count() < result.incumbent_bins) {
      result.incumbent = dived;
      result.incumbent_bins = dived.bin_count();
    }
  }
}

#ifdef BPP_HAS_CPLEX
// Legacy SAFE_MIP_SOL (DP_POP.cpp:910-978), matching the paper's own
// closing step (Sec. 4: "how we solve the reduced SC problem to optimality
// ... thanks to a simplified version of our BPC algorithm"). Once
// populate_root_columns has enumerated every pattern whose reduced cost
// could conceivably matter (any pattern that could be part of a solution
// beating the incumbent, given the certified duals), solving a genuine 0/1
// MIP over that pool -- not just its LP relaxation -- either finds a
// strictly better integer solution, or CPLEX's exact branch-and-cut proves
// infeasible, which is by itself a full, self-contained proof that the
// incumbent is optimal: independent of, and not weakened by, whatever the
// raw LP/dual bound says. Root-only (mirrors legacy's `level==0` gate on
// this same mechanism, BPPS_BP_TREE.cpp:3832-3851) and CPLEX-only for now
// (Gurobi MIP support not yet added to GurobiRmp).
//
// Bounded to a handful of rounds (populate, then MIP-check, repeating only
// if the MIP found a genuine improvement) rather than looping until
// exhaustion -- each round enumerates and solves a fresh MIP, so an
// unbounded loop on a pathological instance would be an unbounded cost;
// legacy's own single populate-then-MIP-solve step is the baseline this
// mirrors, so a handful of rounds is already an extension of it, not a
// truncation.
void try_safe_mip_certification(const Instance& instance, const ColumnGenerationOptions& options,
                                ColumnGenerationResult& result) {
  if (!options.branching.constraints().empty()) return;
  if (options.backend != LpBackend::Cplex) return;
  constexpr int kMaxRounds = 5;
  for (int round = 0; round < kMaxRounds; ++round) {
    if (result.safe_bound.has_value() && result.safe_duals_feasible &&
        result.safe_bound->ceil_bins() == result.incumbent_bins) {
      return;  // Already certified via the standard LP/dual route.
    }
    if (result.incumbent_bins <= 1) return;  // Nothing to beat.

    ColumnGenerationOptions populate_options = options;
    populate_options.sr3_cuts = result.active_sr3_cuts;
    // Cap enumeration far below options.populate_max_columns' own default
    // (500000): the MIP below has its own 20s time limit, so a pool much
    // larger than a few thousand columns is not going to be solvable
    // within it anyway -- keep enumeration itself bounded accordingly
    // rather than spending time generating columns the MIP step could
    // never use in time.
    populate_options.populate_max_columns =
        std::min<std::size_t>(options.populate_max_columns, 20000);
    const auto columns_before = result.patterns.size();
    populate_root_columns(instance, populate_options, result);
    if (result.patterns.size() == columns_before && round > 0) {
      // No new patterns to add and this isn't the first attempt: further
      // rounds cannot change the outcome.
      return;
    }
    // populate_root_columns' own enumeration-completeness argument is what
    // makes an "infeasible" MIP verdict below a genuine optimality proof
    // (see this function's header comment); if the column cap or the
    // enumeration deadline cut it off early, the pool may be missing a
    // pattern that would matter, so an "infeasible" verdict over an
    // incomplete pool would NOT be a valid proof -- stop here rather than
    // risk certifying on an incomplete search.
    if (!result.populate_complete) return;

    CplexRmp mip(instance, result.active_sr3_cuts);
    for (const auto& pattern : result.patterns.patterns()) mip.add_pattern(pattern);
    std::optional<std::vector<std::size_t>> selection;
    try {
      selection = mip.solve_mip_at_most(static_cast<std::size_t>(result.incumbent_bins) - 1);
    } catch (const std::exception&) {
      // The MIP's own time limit (solve_mip_at_most) was hit without a
      // conclusive verdict: this pool/instance is not (yet) tractable this
      // way. Give up on certification via this route rather than letting
      // an inconclusive MIP status abort the whole solve -- the caller
      // still has whatever LP-based bound/incumbent was already found.
      return;
    }
    if (!selection.has_value()) {
      // Proven: no selection of the enumerated pool achieves fewer than
      // incumbent_bins patterns -- and by populate_root_columns' own
      // enumeration-completeness argument (every pattern whose reduced
      // cost could matter is already in the pool), this proves
      // incumbent_bins is the true optimum, independent of the LP bound.
      result.safe_bound = SafeBound(result.incumbent_bins, 1);
      result.safe_duals_feasible = true;
      result.converged = true;
      return;
    }
    // A covering (not partitioning) MIP can select overlapping patterns
    // (an item covered twice); reuse round_master_solution's existing,
    // already-tested x=1 selection/conflict handling (best-fit-decreasing
    // on the residual) instead of assuming the raw selection is already a
    // valid disjoint packing.
    std::vector<double> selection_values(result.patterns.size(), 0.0);
    for (auto index : *selection) selection_values[index] = 1.0;
    const auto improved = round_master_solution(instance, result.patterns.patterns(), selection_values);
    // solve_mip_at_most's own constraint guarantees |selection| <=
    // incumbent_bins - 1, and round_master_solution never needs more bins
    // than the patterns it was handed plus best-fit-decreasing leftovers,
    // so this is expected to always improve; the explicit check is a
    // defensive guard against that assumption, not the expected path.
    if (improved.bin_count() < result.incumbent_bins) {
      result.incumbent = improved;
      result.incumbent_bins = improved.bin_count();
      continue;  // Tighter target now available: try to certify or improve again.
    }
    return;
  }
}
#endif

void validate_column_generation_options(const ColumnGenerationOptions& options) {
  if (options.max_iterations == 0) throw std::invalid_argument("column-generation iteration limit must be positive");
  if (options.populate_max_columns == 0) {
    throw std::invalid_argument("populate column limit must be positive");
  }
  if (!std::isfinite(options.reduced_cost_tolerance) || options.reduced_cost_tolerance < 0.0) {
    throw std::invalid_argument("column-generation tolerance must be finite and non-negative");
  }
  if (!std::isfinite(options.stabilization_alpha) || options.stabilization_alpha < 0.0 ||
      options.stabilization_alpha >= 1.0) {
    throw std::invalid_argument("stabilization alpha must be finite and in [0,1)");
  }
  if (options.max_columns_per_iteration == 0) {
    throw std::invalid_argument("pricing column batch limit must be positive");
  }
  if (options.safe_scale <= 0) {
    throw std::invalid_argument("safe-phase scale must be positive");
  }
  if (options.max_sr3_cuts_per_round == 0 || options.max_sr3_separation_rounds == 0) {
    throw std::invalid_argument("SR3 separation limits must be positive");
  }
  if (!std::isfinite(options.sr3_tolerance) || options.sr3_tolerance < 0.0) {
    throw std::invalid_argument("SR3 tolerance must be finite and non-negative");
  }
  if (options.max_sr3_cuts_total == 0) {
    throw std::invalid_argument("SR3 total cut cap must be positive");
  }
  if (std::isnan(options.sr3_gap_activation) || options.sr3_gap_activation < 0.0) {
    throw std::invalid_argument("SR3 gap-activation threshold must be non-negative");
  }
}

// Runs floating-point pricing/RMP iterations on an already-built master and
// pattern pool for up to options.max_iterations *new* iterations, reusing
// whatever cuts are already attached to `master` (queried live, not from
// `options.sr3_cuts`, so this is correct whether the cuts were baked in at
// construction or appended afterwards via MasterRmp::add_cut). Leaves
// result.converged true only if pricing certified no improving column within
// this call's iteration budget.
// Historical static dual-value smoothing ("SMOOTHING_UPDATE_PI",
// BPPS_BP_MASTER.cpp:2965-2988): pricing_duals[j] = alpha*center[j] +
// (1-alpha)*pi_real[j], where `center` starts at the first solve's real
// duals ("SMOOTHING_INIT", BPPS_BP_MASTER.cpp:2977-2988) and is only ever
// advanced to a newer dual vector once a column priced under it is
// confirmed genuinely improving under the *real* duals too (the misprice
// safeguard below). Legacy's own center update additionally rescales via a
// Farley-bound-gated normalization tied to bookkeeping this codebase does
// not have (BPPS_BP_MASTER.cpp:3005-3040); this uses the simpler, standard
// "advance center to the newest validated real dual" rule instead -- a
// documented simplification, not a silent deviation.
//
// Gated to the root, cut-free case only, mirroring legacy's own gate
// exactly (BPPS_BP_MASTER.cpp:3864-3886: PARAM_SMOOTH==1 && pure BPP &&
// count_triplets==0 && n_conflict_active==0 && level==0 && !diving --
// `options.branching.constraints().empty()` is this codebase's equivalent
// of "level==0", since every branch-and-price node below the root
// necessarily has at least one Ryan-Foster constraint). Confirmed via every
// parameter file found (including the paper_v* variants) having
// PARAM_SMOOTH=0: this was never exercised in producing the paper's
// reported results either, which is why it stays opt-in and off by default
// (ColumnGenerationOptions::dual_stabilization) rather than becoming part
// of the default solve path.
double stabilized_dual_value(const std::vector<double>& duals, const Pattern& pattern) {
  double value = 0.0;
  for (int item : pattern.items()) value += duals[static_cast<std::size_t>(item)];
  return value;
}

void run_floating_pricing_loop(const Instance& instance,
                               const ColumnGenerationOptions& options,
                               MasterRmp& master, FloatingRootPricer& pricer,
                               ColumnGenerationResult& result) {
  result.converged = false;
  bool stabilization_active = options.dual_stabilization && options.branching.constraints().empty();
  std::vector<double> stab_center;
  for (std::size_t local_iteration = 0; local_iteration < options.max_iterations; ++local_iteration) {
    master.solve();
    result.master_values = master.primal_values();
    const std::vector<double> pi_real = master.duals();
    const bool has_cuts = !master.sr3_cuts().empty();
    if (has_cuts) stabilization_active = false;  // legacy: count_triplets==0 required
    std::vector<double> pricing_duals = pi_real;
    if (stabilization_active) {
      if (stab_center.empty()) stab_center = pi_real;  // SMOOTHING_INIT
      for (std::size_t item = 0; item < pricing_duals.size(); ++item) {
        pricing_duals[item] = options.stabilization_alpha * stab_center[item] +
                              (1.0 - options.stabilization_alpha) * pi_real[item];
      }
    }
    result.lp_bound = master.objective_value();
    ++result.iterations;
    const auto certified = !has_cuts
                               ? certify_scaled_duals(result.patterns.patterns(), master.duals(), 1000000000)
                               : certify_scaled_duals(result.patterns.patterns(), master.duals(),
                                                      master.sr3_cuts(), master.sr3_duals(), 1000000000);
    result.safe_duals_feasible = certified.has_value();
    if (certified.has_value()) result.safe_bound = *certified;
    else result.safe_bound.reset();
    const auto candidate = round_master_solution(instance, result.patterns.patterns(), master.primal_values());
    if (candidate.bin_count() < result.incumbent_bins) {
      result.incumbent = candidate;
      result.incumbent_bins = candidate.bin_count();
    }
    const auto dived = dive_master_solution(instance, result.patterns.patterns(), master.primal_values());
    if (dived.bin_count() < result.incumbent_bins) {
      result.incumbent = dived;
      result.incumbent_bins = dived.bin_count();
    }
    if (!options.branching.constraints().empty()) {
      std::vector<Sr3Cut> active_cuts;
      if (has_cuts) {
        active_cuts = master.sr3_cuts();
        for (std::size_t cut = 0; cut < active_cuts.size(); ++cut) {
          active_cuts[cut].dual = master.sr3_duals()[cut];
        }
      }
      // Batched branch-aware label-setting DP, not the one-column-per-call
      // DFS (price(...,branching)/price_with_branching_and_sr3): the DFS was
      // the branch-and-price tree's dominant bottleneck once a node has any
      // active Ryan-Foster constraint, see docs/STATUS.md/
      // REPORT_REFACTORING_BPP.tex for the measured gap this closes.
      const auto priced = pricer.price_label_setting_with_branching_and_sr3(
          instance, pricing_duals, options.branching, active_cuts,
          options.max_columns_per_iteration);
      if (priced.empty() || priced.front().reduced_cost >= -options.reduced_cost_tolerance) {
        result.converged = true;
        return;
      }
      std::size_t added = 0;
      for (const auto& candidate_pattern : priced) {
        if (!candidate_pattern.pattern.has_value() ||
            candidate_pattern.reduced_cost >= -options.reduced_cost_tolerance) continue;
        const auto before = result.patterns.size();
        result.patterns.add(candidate_pattern.pattern->items());
        if (result.patterns.size() == before) continue;
        master.add_pattern(result.patterns.at(result.patterns.size() - 1));
        ++result.generated_columns;
        ++added;
      }
      if (added == 0) {
        result.converged = true;
        return;
      }
    } else if (has_cuts) {
      auto active_cuts = master.sr3_cuts();
      for (std::size_t cut = 0; cut < active_cuts.size(); ++cut) {
        active_cuts[cut].dual = master.sr3_duals()[cut];
      }
      // Batched label-setting DP with per-cut count state, not the DFS
      // (price_with_sr3): the DFS only ever prices one column per RMP
      // re-solve and degrades badly on real-sized instances (see
      // docs/STATUS.md for the measured gap this closes).
      const auto priced = pricer.price_label_setting_with_sr3(instance, pricing_duals,
                                                              active_cuts,
                                                              options.max_columns_per_iteration);
      if (priced.empty() || priced.front().reduced_cost >= -options.reduced_cost_tolerance) {
        result.converged = true;
        return;
      }
      std::size_t added = 0;
      for (const auto& candidate_pattern : priced) {
        if (!candidate_pattern.pattern.has_value() ||
            candidate_pattern.reduced_cost >= -options.reduced_cost_tolerance) continue;
        const auto before = result.patterns.size();
        result.patterns.add(candidate_pattern.pattern->items());
        if (result.patterns.size() == before) continue;
        master.add_pattern(result.patterns.at(result.patterns.size() - 1));
        ++result.generated_columns;
        ++added;
      }
      if (added == 0) {
        result.converged = true;
        return;
      }
    } else {
      auto price_plain = [&](const std::vector<double>& duals) {
        return pricer.price_label_setting(instance, duals, options.max_columns_per_iteration);
      };
      auto priced = price_plain(pricing_duals);

      if (stabilization_active) {
        const bool improving_smoothed =
            !priced.empty() && priced.front().reduced_cost < -options.reduced_cost_tolerance;
        if (!improving_smoothed) {
          // Smoothed duals claim optimality: legacy deactivates stabilization
          // and forces one confirming pass under the real duals before ever
          // declaring convergence (BPPS_BP_MASTER.cpp:4567-4573).
          stabilization_active = false;
          priced = price_plain(pi_real);
        } else {
          // Misprice safeguard (BPPS_BP_MASTER.cpp:4356-4378): a column found
          // under smoothed duals is only kept if it is still improving under
          // the real duals, not just the smoothed ones used to find it.
          std::vector<PricingResult> validated;
          for (const auto& candidate : priced) {
            if (!candidate.pattern.has_value()) continue;
            const double bin_cost = candidate.dual_value + candidate.reduced_cost;
            const double real_value = stabilized_dual_value(pi_real, *candidate.pattern);
            const double real_reduced_cost = bin_cost - real_value;
            if (real_reduced_cost < -options.reduced_cost_tolerance) {
              PricingResult accepted = candidate;
              accepted.dual_value = real_value;
              accepted.reduced_cost = real_reduced_cost;
              validated.push_back(std::move(accepted));
            }
          }
          if (validated.empty()) {
            // Every smoothed-improving candidate mispriced under the real
            // duals: no genuine progress confirmed this round. Fall back and
            // retry immediately with the real duals already computed above,
            // instead of wasting an iteration or declaring false convergence.
            stabilization_active = false;
            priced = price_plain(pi_real);
          } else {
            std::sort(validated.begin(), validated.end(),
                      [](const PricingResult& left, const PricingResult& right) {
                        return left.reduced_cost < right.reduced_cost;
                      });
            priced = std::move(validated);
            // Advance the center now that a column has been genuinely
            // validated against the newest real duals (see the function's
            // header comment for how this differs from legacy's
            // Farley-bound-gated rescaling).
            stab_center = pi_real;
          }
        }
      }

      if (priced.empty() || priced.front().reduced_cost >= -options.reduced_cost_tolerance) {
        result.converged = true;
        return;
      }
      std::size_t added = 0;
      for (const auto& candidate_pattern : priced) {
        if (!candidate_pattern.pattern.has_value() ||
            candidate_pattern.reduced_cost >= -options.reduced_cost_tolerance) continue;
        const auto before = result.patterns.size();
        result.patterns.add(candidate_pattern.pattern->items());
        if (result.patterns.size() == before) continue;
        master.add_pattern(result.patterns.at(result.patterns.size() - 1));
        ++result.generated_columns;
        ++added;
      }
      // The historical DP returns zero when all selected negative columns are
      // already forbidden/present; the master loop treats that as pricing
      // convergence and proceeds to its safe phase.
      if (added == 0) {
        result.converged = true;
        return;
      }
    }
  }
}

std::vector<std::vector<int>> branch_seed_patterns(const Instance& instance,
                                                    const BranchingState& state) {
  const std::size_t n = instance.item_count();
  std::vector<int> parent(n);
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> find = [&](int item) {
    while (parent[static_cast<std::size_t>(item)] != item) {
      parent[static_cast<std::size_t>(item)] =
          parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(item)])];
      item = parent[static_cast<std::size_t>(item)];
    }
    return item;
  };
  for (const auto& constraint : state.constraints()) {
    if (constraint.relation == PairRelation::Together) {
      const int left = find(constraint.first);
      const int right = find(constraint.second);
      if (left != right) parent[static_cast<std::size_t>(right)] = left;
    }
  }
  std::map<int, std::vector<int>> groups;
  for (int item = 0; item < static_cast<int>(n); ++item) groups[find(item)].push_back(item);

  std::vector<std::vector<int>> seeds;
  for (const auto& entry : groups) {
    Pattern candidate(instance, entry.second);
    if (candidate.load() <= instance.capacity() && state.accepts(candidate)) {
      seeds.push_back(entry.second);
    }
  }
  return seeds;
}

// Builds the pattern pool a node's column generation starts from. With a
// non-empty `options.warm_start_patterns` (set by the depth-first tree
// driver in branch_and_price.cpp), this reuses whichever previously-priced
// patterns are still feasible under the current branching state instead of
// only the singleton/branch-seed columns every node would otherwise start
// from — see the field comment on ColumnGenerationOptions::warm_start_patterns.
PatternPool initialize_pattern_pool(const Instance& instance,
                                    const ColumnGenerationOptions& options) {
  if (!options.warm_start_patterns.empty()) {
    PatternPool pool(instance);
    for (const auto& seed : options.warm_start_patterns) {
      Pattern candidate(instance, seed);
      if (candidate.load() <= instance.capacity() && options.branching.accepts(candidate)) {
        pool.add(seed);
      }
    }
    if (pool.size() == 0) {
      throw std::runtime_error("warm-start pattern pool has no feasible seed pattern");
    }
    return pool;
  }
  if (options.branching.constraints().empty()) {
    return PatternPool::with_singletons(instance);
  }
  PatternPool pool(instance);
  for (const auto& seed : branch_seed_patterns(instance, options.branching)) {
    pool.add(seed);
  }
  if (pool.size() == 0) {
    throw std::runtime_error("branching node has no feasible seed pattern");
  }
  return pool;
}

#ifdef BPP_HAS_SOPLEX
// Runs Algorithm 1 (Baldacci, Coniglio, Cordeau, Furini, INFORMS JOC 2023,
// Sec. 3.3) using `master` as the current LP solver: prices under the
// *scaled-integer* duals every iteration (not raw floating ones), which
// gives an exact diminished reduced cost and a proven-valid fathoming
// bound (LBF, Proposition 3) for free every iteration -- see
// docs/STATUS.md's "previous checkpoint's numbers retracted; the real
// mechanism found via the paper's own Algorithm 1" checkpoint for the full
// derivation, the legacy-source cross-check and the measurements this is
// based on. Root/SR3 case only (no branching): `master` must be either
// MasterRmp (phase I, floating) or SoplexRmp (phase II, rational).
//
// Returns true once genuinely converged or fathomed (result.converged and
// result.safe_bound are set either way -- a fathoming bound is just as
// usable for pruning as a fully-converged one). Returns false only when
// `epsilon_scaled > 0` (i.e. `master` is the floating solver) and pricing
// found a genuinely improving column whose scaled reduced cost fell within
// `epsilon_scaled` of zero: the caller must then switch to the rational
// solver and call this again with that solver and `epsilon_scaled = 0`
// (phase II never switches back, matching the paper).
template <typename Rmp>
bool run_algorithm1_loop(const Instance& instance, const ColumnGenerationOptions& options,
                         Rmp& master, PatternPool& patterns, const std::vector<Sr3Cut>& cuts,
                         std::int64_t epsilon_scaled, ColumnGenerationResult& result) {
  const std::int64_t scale = options.safe_scale;
  for (std::size_t iteration = 0; iteration < options.max_iterations; ++iteration) {
    master.solve();
    result.master_values = master.primal_values();
    result.lp_bound = master.objective_value();
    ++result.iterations;

    const auto scaled_items = safe_integer_duals(master.duals(), scale);
    const auto scaled_cut_duals =
        cuts.empty() ? std::vector<std::int64_t>{} : safe_integer_duals(master.sr3_duals(), scale);

    // z_dim: the raw diminished-dual sum, always defined (Sec. 3.1),
    // regardless of whether (p_int, r_int) is currently dual-feasible.
    std::vector<std::int64_t> combined = scaled_items;
    combined.insert(combined.end(), scaled_cut_duals.begin(), scaled_cut_duals.end());
    const SafeBound z_dim = SafeBound::from_scaled_duals(combined, scale);

    const auto rounded = round_master_solution(instance, patterns.patterns(), master.primal_values());
    if (rounded.bin_count() < result.incumbent_bins) {
      result.incumbent = rounded;
      result.incumbent_bins = rounded.bin_count();
    }
    const auto dived = dive_master_solution(instance, patterns.patterns(), master.primal_values());
    if (dived.bin_count() < result.incumbent_bins) {
      result.incumbent = dived;
      result.incumbent_bins = dived.bin_count();
    }

    std::vector<Sr3Cut> scaled_cut_objs = cuts;
    for (std::size_t c = 0; c < scaled_cut_objs.size(); ++c) {
      scaled_cut_objs[c].dual = static_cast<double>(scaled_cut_duals[c]);
    }
    const auto priced = options.branching.constraints().empty()
        ? price_scaled_integer_with_sr3(instance, scaled_items, scale, scaled_cut_objs,
                                        options.max_columns_per_iteration)
        : price_scaled_integer_with_branching_and_sr3(instance, scaled_items, scale, options.branching,
                                                       scaled_cut_objs, options.max_columns_per_iteration);

    if (priced.empty()) {
      // c_min >= 0: (p_int, r_int) is dual-feasible over the *whole*
      // pricing space (the DP is exhaustive, not pool-limited), so z_dim is
      // already a valid, exact certificate -- no further solve needed.
      result.converged = true;
      result.safe_duals_feasible = true;
      result.safe_bound = z_dim;
      return true;
    }

    const double rc_double = priced.front().reduced_cost;
    const auto rc_int = static_cast<std::int64_t>(std::llround(rc_double));
    if (std::abs(rc_double - static_cast<double>(rc_int)) > 0.5) {
      throw std::overflow_error("scaled-integer pricing reduced cost lost exactness");
    }

    // Fathoming Rules 1/2 (Sec. 3.2.1): LBF is a proven-valid lower bound
    // whenever c_min < 0 (Proposition 3). If its ceiling already fathoms
    // against the incumbent, or already ties z_dim's own ceiling, no further
    // resolve can change the answer -- stop now with LBF as the certificate.
    const SafeBound lbf = z_dim.fathoming_bound(rc_int, scale);
    if (lbf.ceil_bins() >= result.incumbent_bins || lbf.ceil_bins() == z_dim.ceil_bins()) {
      result.converged = true;
      result.safe_duals_feasible = true;
      result.safe_bound = lbf;
      return true;
    }

    std::size_t added = 0;
    for (const auto& candidate : priced) {
      if (!candidate.pattern.has_value()) continue;
      const auto before = patterns.size();
      patterns.add(candidate.pattern->items());
      if (patterns.size() == before) continue;
      master.add_pattern(patterns.at(patterns.size() - 1));
      ++result.generated_columns;
      ++added;
    }
    if (added == 0) {
      result.converged = true;
      result.safe_duals_feasible = true;
      result.safe_bound = z_dim;
      return true;
    }

    // Precision switch (Sec. 3.3, line 18-19): once the best improving
    // column's scaled reduced cost is within epsilon_scaled of zero, the
    // floating solver's own tolerance can no longer be trusted to bring it
    // into the basis correctly -- switch to the rational solver from here.
    if (epsilon_scaled > 0 && rc_int >= -epsilon_scaled) return false;
  }
  throw std::runtime_error("Algorithm 1 exceeded its iteration budget");
}

// Driver implementing Algorithm 1 end to end, for both the root (empty
// branching) and branch-and-price node (non-empty BranchingState) cases --
// see run_algorithm1_loop's branching dispatch to
// price_scaled_integer_with_branching_and_sr3 above.
ColumnGenerationResult solve_root_column_generation_algorithm1(
    const Instance& instance, const ColumnGenerationOptions& options) {
  validate_column_generation_options(options);

  ColumnGenerationResult result(instance);
  result.patterns = initialize_pattern_pool(instance, options);
  const auto greedy = solve_best_fit_decreasing(instance);
  bool greedy_allowed = true;
  if (!options.branching.constraints().empty()) {
    for (const auto& bin : greedy.bins()) {
      Pattern pattern(instance, bin.items);
      if (!options.branching.accepts(pattern)) {
        greedy_allowed = false;
        break;
      }
    }
  }
  if (greedy_allowed) {
    result.incumbent = greedy;
    result.incumbent_bins = greedy.bin_count();
  } else {
    result.incumbent_bins = INT_MAX;
  }
  // SoPlex is the safe backend this build certifies with, whether or not
  // phase II ends up actually being entered (see run_algorithm1_loop's doc
  // comment: reaching an exact certificate in phase I alone, with zero
  // phase-II iterations, is a legitimate outcome now, not a special case).
  result.phase2_backend = "soplex";

  std::vector<Sr3Cut> active_cuts = options.sr3_cuts;
  std::size_t cuts_added = 0;
  const bool can_separate = options.automatic_sr3_separation && options.sr3_cuts.empty();
  const auto epsilon_scaled = static_cast<std::int64_t>(
      options.reduced_cost_tolerance * static_cast<double>(options.safe_scale)) + 1;

  bool phase2 = false;
  for (std::size_t round = 0;; ++round) {
    bool done;
    if (!phase2) {
      MasterRmp master(options.backend, instance, active_cuts);
      for (const auto& pattern : result.patterns.patterns()) master.add_pattern(pattern);
      const auto before = result.iterations;
      done = run_algorithm1_loop(instance, options, master, result.patterns, active_cuts,
                                 epsilon_scaled, result);
      result.phase1_iterations += result.iterations - before;
      if (!done) phase2 = true;
    } else {
      done = false;
    }
    if (!done) {
      SoplexRmp rational_master(instance, active_cuts);
      for (const auto& pattern : result.patterns.patterns()) rational_master.add_pattern(pattern);
      const auto before = result.iterations;
      done = run_algorithm1_loop(instance, options, rational_master, result.patterns, active_cuts,
                                 0, result);
      result.phase2_iterations += result.iterations - before;
    }
    if (!done) throw std::logic_error("Algorithm 1 phase II failed to converge");
    result.phase2_verified = result.safe_duals_feasible;

    if (!can_separate) break;
    if (cuts_added >= options.max_sr3_cuts_total) break;
    const double gap = static_cast<double>(result.incumbent_bins) - result.lp_bound;
    if (!(gap < options.sr3_gap_activation)) break;
    const auto max_cuts_this_round = std::min<std::size_t>(
        options.max_sr3_cuts_per_round, options.max_sr3_cuts_total - cuts_added);
    const auto violations = separate_sr3(instance, result.patterns.patterns(), result.master_values,
                                         max_cuts_this_round, options.sr3_tolerance);
    if (violations.empty()) break;
    for (const auto& violation : violations) {
      active_cuts.push_back(violation.cut);
      ++cuts_added;
    }
    if (round + 1 >= options.max_sr3_separation_rounds) break;
  }
  result.active_sr3_cuts = active_cuts;
  result.sr3_cuts_added = cuts_added;

  if (options.populate && result.converged && options.branching.constraints().empty()) {
    ColumnGenerationOptions populate_options = options;
    populate_options.sr3_cuts = active_cuts;
    populate_root_columns(instance, populate_options, result);
  }
#ifdef BPP_HAS_CPLEX
  // Legacy SAFE_MIP_SOL (see try_safe_mip_certification's doc comment):
  // a last-resort exact certification, tried only when the standard LP/
  // dual route above did not already certify. Unconditional (not gated on
  // options.populate) as of the enumeration rewrite onto the dominance-
  // pruned pricing DP: every historical parameter file found runs this by
  // default (it is not a tunable extra, it is Sec. 4 of the paper itself),
  // and it is now bounded and fast (measured 5.6-9.7s on every instance
  // that previously needed it, full ANI-201 sample) rather than the
  // unbounded DFS enumeration that made it unsafe to run unconditionally
  // before that fix. A no-op (single ceil_bins check, immediate return)
  // whenever the standard route already certified, so this costs nothing
  // on the common case.
  if (result.converged && options.branching.constraints().empty()) {
    ColumnGenerationOptions mip_options = options;
    mip_options.sr3_cuts = active_cuts;
    try_safe_mip_certification(instance, mip_options, result);
  }
#endif
  return result;
}

void run_soplex_safe_phase(const Instance& instance,
                           const ColumnGenerationOptions& options,
                           ColumnGenerationResult& result) {
  result.phase2_backend = "soplex";
  SoplexRmp master(instance, options.sr3_cuts);
  for (const auto& pattern : result.patterns.patterns()) master.add_pattern(pattern);

  FixedPointRootPricer fixed_pricer(options.safe_scale);
  FloatingRootPricer floating_pricer;
  for (std::size_t iteration = 0; iteration < options.max_iterations; ++iteration) {
    master.solve();
    result.master_values = master.primal_values();
    result.lp_bound = master.objective_value();
    result.phase2_iterations = iteration + 1;
    result.iterations = result.phase1_iterations + result.phase2_iterations;

    const auto& item_duals = master.duals();
    std::optional<SafeBound> certified;
    if (options.sr3_cuts.empty()) {
      certified = certify_scaled_duals(result.patterns.patterns(), item_duals,
                                       options.safe_scale);
    } else {
      certified = certify_scaled_duals(result.patterns.patterns(), item_duals,
                                       options.sr3_cuts, master.sr3_duals(),
                                       options.safe_scale);
    }
    result.safe_duals_feasible = certified.has_value();
    if (certified.has_value()) result.safe_bound = *certified;
    else result.safe_bound.reset();

    const auto rounded = round_master_solution(instance, result.patterns.patterns(),
                                                master.primal_values());
    if (rounded.bin_count() < result.incumbent_bins) {
      result.incumbent = rounded;
      result.incumbent_bins = rounded.bin_count();
    }
    const auto dived = dive_master_solution(instance, result.patterns.patterns(),
                                             master.primal_values());
    if (dived.bin_count() < result.incumbent_bins) {
      result.incumbent = dived;
      result.incumbent_bins = dived.bin_count();
    }

    const bool use_fixed = options.branching.constraints().empty() && options.sr3_cuts.empty();
    PricingResult floating_price;
    FixedPointPricingResult fixed_price;
    if (use_fixed) {
      fixed_price = fixed_pricer.price(instance, safe_integer_duals(item_duals, options.safe_scale));
      if (fixed_price.reduced_cost >= 0) {
        result.phase2_verified = certified.has_value();
        result.converged = result.phase2_verified;
        break;
      }
      const auto before = result.patterns.size();
      result.patterns.add(fixed_price.pattern->items());
      if (result.patterns.size() == before) break;
      master.add_pattern(result.patterns.at(result.patterns.size() - 1));
      ++result.generated_columns;
      continue;
    }

    std::vector<Sr3Cut> active_cuts;
    if (!options.sr3_cuts.empty()) {
      active_cuts = options.sr3_cuts;
      for (std::size_t cut = 0; cut < active_cuts.size(); ++cut) {
        active_cuts[cut].dual = master.sr3_duals()[cut];
      }
    }
    if (options.branching.constraints().empty() && active_cuts.empty()) {
      floating_price = floating_pricer.price(instance, item_duals);
      if (!floating_price.pattern.has_value() ||
          floating_price.reduced_cost >= -options.reduced_cost_tolerance) {
        result.phase2_verified = certified.has_value();
        result.converged = result.phase2_verified;
        break;
      }
      const auto before = result.patterns.size();
      result.patterns.add(floating_price.pattern->items());
      if (result.patterns.size() == before) break;
      master.add_pattern(result.patterns.at(result.patterns.size() - 1));
      ++result.generated_columns;
      continue;
    }

    // Batched pricing, not one column per SoPlex resolve: a rational resolve
    // costs roughly two orders of magnitude more than a CPLEX one (measured
    // ~550ms vs. ~2ms, docs/STATUS.md 2026-08-07 "general speed gap"
    // checkpoint), so amortizing it over a whole batch of improving columns
    // -- exactly like phase 1 already does -- matters far more here than in
    // the floating phase. Only the SR3-cuts/no-branching case is batched for
    // now (the case actually measured); the branching case still prices one
    // column per call, unchanged.
    if (options.branching.constraints().empty()) {
      const auto priced = floating_pricer.price_label_setting_with_sr3(
          instance, item_duals, active_cuts, options.max_columns_per_iteration);
      if (priced.empty() || priced.front().reduced_cost >= -options.reduced_cost_tolerance) {
        result.phase2_verified = certified.has_value();
        result.converged = result.phase2_verified;
        break;
      }
      std::size_t added = 0;
      for (const auto& candidate_pattern : priced) {
        if (!candidate_pattern.pattern.has_value() ||
            candidate_pattern.reduced_cost >= -options.reduced_cost_tolerance) continue;
        const auto before = result.patterns.size();
        result.patterns.add(candidate_pattern.pattern->items());
        if (result.patterns.size() == before) continue;
        master.add_pattern(result.patterns.at(result.patterns.size() - 1));
        ++result.generated_columns;
        ++added;
      }
      if (added == 0) {
        result.phase2_verified = certified.has_value();
        result.converged = result.phase2_verified;
        break;
      }
      continue;
    }

    // Batched branch-aware DP, not the DFS: see run_floating_pricing_loop.
    const auto priced = floating_pricer.price_label_setting_with_branching_and_sr3(
        instance, item_duals, options.branching, active_cuts, 1);
    floating_price = priced.empty() ? PricingResult{} : priced.front();
    if (!floating_price.pattern.has_value() ||
        floating_price.reduced_cost >= -options.reduced_cost_tolerance) {
      result.phase2_verified = certified.has_value();
      result.converged = result.phase2_verified;
      break;
    }
    const auto before = result.patterns.size();
    result.patterns.add(floating_price.pattern->items());
    if (result.patterns.size() == before) break;
    master.add_pattern(result.patterns.at(result.patterns.size() - 1));
    ++result.generated_columns;
  }
}
#endif

}  // namespace

ColumnGenerationResult solve_root_column_generation_once(
    const Instance& instance, const ColumnGenerationOptions& options) {
  validate_column_generation_options(options);

  ColumnGenerationResult result(instance);
  result.active_sr3_cuts = options.sr3_cuts;
  result.patterns = initialize_pattern_pool(instance, options);
  MasterRmp master(options.backend, instance, options.sr3_cuts);
  for (const auto& pattern : result.patterns.patterns()) master.add_pattern(pattern);
  FloatingRootPricer pricer;
  const auto greedy = solve_best_fit_decreasing(instance);
  bool greedy_allowed = true;
  if (!options.branching.constraints().empty()) {
    for (const auto& bin : greedy.bins()) {
      Pattern pattern(instance, bin.items);
      if (!options.branching.accepts(pattern)) {
        greedy_allowed = false;
        break;
      }
    }
  }
  if (greedy_allowed) {
    result.incumbent = greedy;
    result.incumbent_bins = greedy.bin_count();
  } else {
    result.incumbent_bins = INT_MAX;
  }
  run_floating_pricing_loop(instance, options, master, pricer, result);
  result.phase1_iterations = result.iterations;
  if (options.populate && result.converged && options.branching.constraints().empty()) {
    populate_root_columns(instance, options, result);
  }
  return result;
}

ColumnGenerationResult solve_root_column_generation(
    const Instance& instance, const ColumnGenerationOptions& options) {
  const bool can_separate = options.automatic_sr3_separation &&
                            options.sr3_cuts.empty();
  if (!can_separate) return solve_root_column_generation_once(instance, options);
  validate_column_generation_options(options);

  // A single master/pattern pool is kept alive across every SR3 round: cuts
  // are appended as new rows on the live LP (MasterRmp::add_cut) instead of
  // rebuilding the whole RMP and re-discovering every column from scratch.
  // Each add_cut is a warm-started re-optimization from the previous basis,
  // mirroring the historical CPXaddrows-into-the-persistent-master approach
  // (BPPS_BP_TRIPLETS.cpp:557-651) instead of the legacy-incompatible
  // "rebuild everything per round" behavior this replaces.
  ColumnGenerationResult result(instance);
  result.patterns = initialize_pattern_pool(instance, options);
  MasterRmp master(options.backend, instance, {});
  for (const auto& pattern : result.patterns.patterns()) master.add_pattern(pattern);
  FloatingRootPricer pricer;
  const auto greedy = solve_best_fit_decreasing(instance);
  bool greedy_allowed = true;
  if (!options.branching.constraints().empty()) {
    for (const auto& bin : greedy.bins()) {
      Pattern pattern(instance, bin.items);
      if (!options.branching.accepts(pattern)) {
        greedy_allowed = false;
        break;
      }
    }
  }
  if (greedy_allowed) {
    result.incumbent = greedy;
    result.incumbent_bins = greedy.bin_count();
  } else {
    result.incumbent_bins = INT_MAX;
  }
  std::vector<Sr3Cut> active_cuts;
  std::size_t cuts_added = 0;
  auto finish_converged = [&]() {
    if (options.populate && options.branching.constraints().empty()) {
      // populate_root_columns reads cuts from options.sr3_cuts and rebuilds
      // its own one-shot master; feed it the cuts actually accumulated on
      // the live `master` above, not the (always-empty here) caller options.
      ColumnGenerationOptions populate_options = options;
      populate_options.sr3_cuts = active_cuts;
      populate_root_columns(instance, populate_options, result);
    }
    return result;
  };

  for (std::size_t round = 0; round < options.max_sr3_separation_rounds; ++round) {
    run_floating_pricing_loop(instance, options, master, pricer, result);
    result.active_sr3_cuts = active_cuts;
    result.sr3_cuts_added = cuts_added;
    if (!result.converged || result.master_values.empty()) return result;

    // Legacy PARAM_MAX_TRIPLETS gate (BPPS_BP_MASTER.cpp:4712): once the
    // cumulative cut budget is exhausted, stop separating for the rest of
    // this call and return the already-converged relaxation unchanged.
    if (cuts_added >= options.max_sr3_cuts_total) return finish_converged();
    // Legacy PARAM_TRIPLET_GAP_ACT gate (BPPS_BP_MASTER.cpp:4705): separation
    // only activates once the incumbent/LP gap has closed below the
    // configured threshold; otherwise skip cutting this round.
    const double gap = static_cast<double>(result.incumbent_bins) - result.lp_bound;
    if (!(gap < options.sr3_gap_activation)) return finish_converged();

    const auto max_cuts_this_round = std::min<std::size_t>(
        options.max_sr3_cuts_per_round, options.max_sr3_cuts_total - cuts_added);
    const auto violations = separate_sr3(instance, result.patterns.patterns(),
                                         result.master_values,
                                         max_cuts_this_round,
                                         options.sr3_tolerance);
    if (violations.empty()) return finish_converged();
    for (const auto& violation : violations) {
      master.add_cut(violation.cut, result.patterns.patterns());
      active_cuts.push_back(violation.cut);
      ++cuts_added;
    }
    if (round + 1 == options.max_sr3_separation_rounds) {
      // The round budget is a cost safety valve on further separation, not a
      // pricing failure: pricing already converged for the cuts added so
      // far (result.converged is true from the last run_floating_pricing_loop
      // call), so the relaxation and its safe bound remain valid even though
      // more violated triplets may still exist and were not incorporated.
      result.active_sr3_cuts = active_cuts;
      result.sr3_cuts_added = cuts_added;
      return finish_converged();
    }
  }
  throw std::logic_error("unreachable SR3 separation loop");
}

ColumnGenerationResult solve_two_phase_root_column_generation(
    const Instance& instance, const ColumnGenerationOptions& options) {
#ifdef BPP_HAS_SOPLEX
  // Algorithm 1 (Baldacci et al. 2023, Sec. 3.3): see
  // solve_root_column_generation_algorithm1's doc comment and
  // docs/STATUS.md's Algorithm-1 checkpoints (root/SR3, then extended to
  // branch-and-price nodes). Covers both the root and branching cases now;
  // the pair of calls below remains only as the non-SoPlex fallback.
  return solve_root_column_generation_algorithm1(instance, options);
#endif
  ColumnGenerationOptions phase_one_options = options;
  phase_one_options.populate = false;
  ColumnGenerationResult result = solve_root_column_generation(instance, phase_one_options);
  if (options.max_iterations == 0) throw std::invalid_argument("column-generation iteration limit must be positive");

  // The phase-one result is already a valid answer for the caller when no
  // CPLEX backend is available; the constructor above throws in that case.
  if (!result.converged) return result;
  // Phase two has its own exact termination condition.  Do not leak the
  // floating-phase convergence flag if safe pricing later stalls or exhausts
  // its iteration budget without a certificate.
  result.converged = false;
  result.phase2_verified = false;
  ColumnGenerationOptions phase_two_options = options;
  phase_two_options.sr3_cuts = result.active_sr3_cuts;
  phase_two_options.automatic_sr3_separation = false;
#ifdef BPP_HAS_SOPLEX
  run_soplex_safe_phase(instance, phase_two_options, result);
  if (options.populate && result.converged && options.branching.constraints().empty()) {
    // Population enlarges the master. Re-certify the enlarged RMP with the
    // rational backend; the pre-population dual certificate is not valid for
    // newly inserted columns.
    const auto columns_before = result.patterns.size();
    populate_root_columns(instance, phase_two_options, result);
    if (result.patterns.size() != columns_before) {
      result.converged = false;
      result.phase2_verified = false;
      run_soplex_safe_phase(instance, phase_two_options, result);
    }
  }
  return result;
#endif
  MasterRmp master(phase_two_options.backend, instance, phase_two_options.sr3_cuts);
  for (const auto& pattern : result.patterns.patterns()) master.add_pattern(pattern);
  const std::int64_t scale = options.safe_scale;
  FixedPointRootPricer fixed_pricer(scale);
  FloatingRootPricer floating_pricer;

  for (std::size_t iteration = 0; iteration < options.max_iterations; ++iteration) {
    master.solve();
    result.master_values = master.primal_values();
    result.phase2_iterations = iteration + 1;
    result.iterations = result.phase1_iterations + result.phase2_iterations;
    std::optional<SafeBound> certified;
    if (phase_two_options.sr3_cuts.empty()) {
      certified = certify_scaled_duals(result.patterns.patterns(), master.duals(), scale);
    } else {
      auto active_cuts = master.sr3_cuts();
      for (std::size_t cut = 0; cut < active_cuts.size(); ++cut) {
        active_cuts[cut].dual = master.sr3_duals()[cut];
      }
      certified = certify_scaled_duals(result.patterns.patterns(), master.duals(),
                                       active_cuts, master.sr3_duals(), scale);
    }
    if (certified.has_value()) result.safe_bound = *certified;
    else result.safe_bound.reset();
    result.safe_duals_feasible = certified.has_value();

    const auto rounded = round_master_solution(instance, result.patterns.patterns(), master.primal_values());
    if (rounded.bin_count() < result.incumbent_bins) {
      result.incumbent = rounded;
      result.incumbent_bins = rounded.bin_count();
    }
    const auto dived = dive_master_solution(instance, result.patterns.patterns(), master.primal_values());
    if (dived.bin_count() < result.incumbent_bins) {
      result.incumbent = dived;
      result.incumbent_bins = dived.bin_count();
    }

    PricingResult floating_price;
    FixedPointPricingResult fixed_price;
    bool has_fixed_price = phase_two_options.branching.constraints().empty() &&
                           phase_two_options.sr3_cuts.empty();
    if (has_fixed_price) {
      fixed_price = fixed_pricer.price(instance, safe_integer_duals(master.duals(), scale));
      if (fixed_price.reduced_cost >= 0) {
        result.phase2_verified = certified.has_value();
        result.converged = result.phase2_verified;
        break;
      }
    } else if (phase_two_options.branching.constraints().empty()) {
      if (phase_two_options.sr3_cuts.empty()) {
        floating_price = floating_pricer.price(instance, master.duals());
      } else {
        auto active_cuts = master.sr3_cuts();
        for (std::size_t cut = 0; cut < active_cuts.size(); ++cut) {
          active_cuts[cut].dual = master.sr3_duals()[cut];
        }
        // DP with per-cut count state, not the DFS: see run_floating_pricing_loop.
        const auto priced = floating_pricer.price_label_setting_with_sr3(
            instance, master.duals(), active_cuts, 1);
        floating_price = priced.empty() ? PricingResult{} : priced.front();
      }
    } else {
      std::vector<Sr3Cut> active_cuts;
      if (!phase_two_options.sr3_cuts.empty()) {
        active_cuts = master.sr3_cuts();
        for (std::size_t cut = 0; cut < active_cuts.size(); ++cut) {
          active_cuts[cut].dual = master.sr3_duals()[cut];
        }
      }
      // Batched branch-aware DP, not the DFS: see run_floating_pricing_loop.
      const auto priced = floating_pricer.price_label_setting_with_branching_and_sr3(
          instance, master.duals(), phase_two_options.branching, active_cuts, 1);
      floating_price = priced.empty() ? PricingResult{} : priced.front();
      if (!floating_price.pattern.has_value() ||
          floating_price.reduced_cost >= -phase_two_options.reduced_cost_tolerance) {
        result.phase2_verified = certified.has_value();
        result.converged = result.phase2_verified;
        break;
      }
    }

    const auto& priced_pattern = has_fixed_price ? *fixed_price.pattern : *floating_price.pattern;
    const auto before = result.patterns.size();
    result.patterns.add(priced_pattern.items());
    if (result.patterns.size() == before) break;
    master.add_pattern(result.patterns.at(result.patterns.size() - 1));
    ++result.generated_columns;
  }
  if (options.populate && result.converged && options.branching.constraints().empty()) {
    populate_root_columns(instance, phase_two_options, result);
  }
  return result;
}

}  // namespace bpp
