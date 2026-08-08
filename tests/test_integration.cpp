// Integration suite: exercises multiple components together through the
// public solve_* entry points and the CPLEX/Gurobi/SoPlex restricted-master
// backends -- automatic SR3 separation, the full column-generation loop,
// branch-and-price, diving, stabilization. The main block below requires a
// CPLEX-enabled build (BPP_HAS_CPLEX, the default LpBackend); a separate
// block further down requires BPP_HAS_GUROBI and exercises MasterRmp with
// LpBackend::Gurobi explicitly, including a CPLEX-vs-Gurobi equivalence
// check when both are built in (running the complete test suite needs
// both; either alone is enough for normal use, see README.md). The
// SoPlex-specific block requires BPP_HAS_SOPLEX. Compiles to an
// always-succeeding empty main() when none of these are defined, so the
// target still exists (and still registers a CTest entry) in the portable
// build, it just has nothing to do there.
// See tests/test_unit.cpp for isolated-component tests and
// tests/test_regression.cpp for tests pinned to specific previously-fixed
// bugs.
#include "bpp/branching.hpp"
#include "bpp/branch_and_price.hpp"
#include "bpp/cuts.hpp"
#include "bpp/cplex_rmp.hpp"
#include "bpp/gurobi_rmp.hpp"
#include "bpp/column_generation.hpp"
#include "bpp/soplex_rmp.hpp"
#include "bpp/pattern.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>

int main() {
#ifdef BPP_HAS_CPLEX
  {
    bpp::ColumnGenerationOptions populate_options;
    populate_options.max_iterations = 10;
    populate_options.populate = true;
    populate_options.populate_max_columns = 4;
    const auto populated = bpp::solve_root_column_generation(
        bpp::Instance(10, {6, 4}), populate_options);
    assert(populated.converged);
    assert(populated.populate_complete || populated.populate_columns == 4);
  }
  {
    // Three pair columns give the canonical violated SR3 row: each pair can
    // carry one half in the restricted master, so the triplet activity is
    // 1.5. The automatic root restart must add that row and expose it.
    bpp::Instance instance(10, {6, 4, 4}, "automatic-sr3");
    bpp::ColumnGenerationOptions options;
    options.max_iterations = 100;
    options.automatic_sr3_separation = true;
    options.max_sr3_cuts_per_round = 1;
    const auto result = bpp::solve_root_column_generation(instance, options);
    assert(result.sr3_cuts_added >= 1);
    assert(!result.active_sr3_cuts.empty());
    assert(result.active_sr3_cuts.front().first == 0);
    assert(result.active_sr3_cuts.front().second == 1);
    assert(result.active_sr3_cuts.front().third == 2);
  }
  {
    // Legacy PARAM_TRIPLET_GAP_ACT (BPPS_BP_MASTER.cpp:4705): separation must
    // stay off while incumbent_bins - lp_bound is not below the threshold,
    // even though a violated triplet exists in the root relaxation.
    bpp::Instance instance(10, {6, 4, 4}, "automatic-sr3-gap-gate");
    bpp::ColumnGenerationOptions options;
    options.max_iterations = 100;
    options.automatic_sr3_separation = true;
    options.max_sr3_cuts_per_round = 1;
    options.sr3_gap_activation = 0.01;
    const auto result = bpp::solve_root_column_generation(instance, options);
    assert(result.sr3_cuts_added == 0);
    assert(result.active_sr3_cuts.empty());
    assert(result.converged);
  }
  {
    // Legacy PARAM_MAX_TRIPLETS (BPPS_BP_MASTER.cpp:4701/4712): once the
    // cumulative cut budget is exhausted, separation must stop adding rows
    // for the rest of the call and still return the converged relaxation.
    bpp::Instance instance(10, {6, 4, 4}, "automatic-sr3-total-cap");
    bpp::ColumnGenerationOptions options;
    options.max_iterations = 100;
    options.automatic_sr3_separation = true;
    options.max_sr3_cuts_per_round = 1;
    options.max_sr3_cuts_total = 0;
    bool threw = false;
    try {
      bpp::solve_root_column_generation(instance, options);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    assert(threw);
    options.max_sr3_cuts_total = 1;
    const auto result = bpp::solve_root_column_generation(instance, options);
    assert(result.sr3_cuts_added == 1);
    assert(result.converged);
  }
  {
    bpp::Instance instance(10, {6, 4});
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    bpp::CplexRmp rmp(instance);
    for (const auto& pattern : pool.patterns()) rmp.add_pattern(pattern);
    rmp.solve();
    assert(rmp.pattern_count() == 3);
    assert(rmp.objective_value() <= 1.0 + 1e-8);
    assert(rmp.duals().size() == 2);
    assert(rmp.primal_values().size() == 3);
  }
  {
    bpp::Instance instance(12, {6, 4, 5});
    bpp::CplexRmp rmp(instance, {bpp::Sr3Cut(0, 1, 2)});
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    pool.add({0, 2});
    for (const auto& pattern : pool.patterns()) rmp.add_pattern(pattern);
    rmp.solve();
    assert(rmp.sr3_cuts().size() == 1);
    assert(rmp.sr3_duals().size() == 1);
    assert(rmp.objective_value() >= 1.0 - 1e-8);
  }
  {
    // Direct CplexRmp::solve_mip_at_most check: 3 items, capacity 10,
    // weights {5,5,5} -- needs 2 bins ({0,1} together, {2} alone). A
    // max_bins of 2 must find that selection; max_bins of 0 forces every
    // pattern variable to zero, which the base RMP's own item-coverage
    // rows (>=1 each) make infeasible -- a real infeasibility proof, not
    // a crash or a hang, which is exactly what try_safe_mip_certification
    // relies on to treat "no selection" as a valid optimality certificate.
    // solve_mip_at_most is one-shot per Rmp instance (it permanently
    // mutates the model -- new row, columns turned binary -- exactly like
    // try_safe_mip_certification's own real usage: a fresh Rmp every
    // round), so each call below gets its own freshly built Rmp rather
    // than reusing one.
    bpp::Instance instance(10, {5, 5, 5}, "cplex-solve-mip-at-most");
    bpp::Pattern pair(instance, {0, 1});
    bpp::Pattern single(instance, {2});
    {
      bpp::CplexRmp rmp(instance);
      rmp.add_pattern(pair);
      rmp.add_pattern(single);
      const auto feasible = rmp.solve_mip_at_most(2);
      assert(feasible.has_value());
      assert(feasible->size() == 2);
    }
    {
      bpp::CplexRmp rmp(instance);
      rmp.add_pattern(pair);
      rmp.add_pattern(single);
      const auto infeasible = rmp.solve_mip_at_most(0);
      assert(!infeasible.has_value());
    }
  }
  {
    bpp::Instance instance(10, {6, 4, 5, 5});
    const auto result = bpp::solve_root_column_generation(instance);
    assert(result.converged);
    assert(std::abs(result.lp_bound - 2.0) < 1e-8);
    assert(result.generated_columns >= 1);
    assert(result.incumbent_bins == 2);
    assert(result.safe_duals_feasible);
    assert(result.safe_bound.has_value());
    assert(result.safe_bound->ceil_bins() <= result.incumbent_bins);
  }
  {
    // Dual-value stabilization (legacy PARAM_SMOOTH, BPPS_BP_MASTER.cpp's
    // SMOOTHING_* routines): off by default, must be requested explicitly,
    // and must reach the same optimum as without it -- it is an alternate
    // dual trajectory towards the same LP, not a different answer. Uses a
    // slightly larger instance than the smoke tests above to give the
    // smoothing/misprice-safeguard/self-deactivation logic more than one
    // column-generation round to exercise.
    bpp::Instance instance(20, {9, 8, 7, 6, 5, 4, 3}, "stabilization");
    const auto baseline = bpp::solve_root_column_generation(instance);
    assert(baseline.converged);

    bpp::ColumnGenerationOptions stabilized_options;
    stabilized_options.dual_stabilization = true;
    assert(std::abs(stabilized_options.stabilization_alpha - 0.3) < 1e-12);
    const auto stabilized = bpp::solve_root_column_generation(instance, stabilized_options);
    assert(stabilized.converged);
    assert(std::abs(stabilized.lp_bound - baseline.lp_bound) < 1e-6);
    assert(stabilized.incumbent_bins == baseline.incumbent_bins);
    assert(stabilized.safe_duals_feasible);
    assert(stabilized.safe_bound.has_value());
    assert(stabilized.safe_bound->ceil_bins() <= stabilized.incumbent_bins);

    // An invalid alpha must be rejected the same way with or without
    // stabilization actually triggering (validated up front, not lazily).
    bpp::ColumnGenerationOptions bad_alpha = stabilized_options;
    bad_alpha.stabilization_alpha = 1.0;
    bool threw_bad_alpha = false;
    try {
      bpp::solve_root_column_generation(instance, bad_alpha);
    } catch (const std::invalid_argument&) {
      threw_bad_alpha = true;
    }
    assert(threw_bad_alpha);

    // Stabilization is gated off (silently, matching legacy's own
    // root-only/cut-free gate) once branching or SR3 cuts are active: the
    // result must still be correct, just without any smoothing applied.
    bpp::ColumnGenerationOptions stabilized_with_branching = stabilized_options;
    stabilized_with_branching.branching = bpp::BranchingState().child(
        bpp::RyanFosterConstraint(0, 1, bpp::PairRelation::Together));
    const auto stabilized_branching_result =
        bpp::solve_root_column_generation(instance, stabilized_with_branching);
    assert(stabilized_branching_result.converged);
  }
  {
    bpp::Instance instance(10, {6, 4, 5, 5});
    bpp::ColumnGenerationOptions options;
    options.max_iterations = 20;
    const auto result = bpp::solve_two_phase_root_column_generation(instance, options);
    assert(result.converged);
    // Under Algorithm 1 (Baldacci et al. 2023, Sec. 3.3; see
    // docs/STATUS.md's Algorithm-1 checkpoint), pricing under scaled-integer
    // duals every iteration means the rational solver is only engaged once
    // the best reduced cost gets numerically small -- for a trivial instance
    // like this one, phase 1 alone can reach an exact certificate, so
    // phase2_iterations == 0 is a legitimate outcome, not a regression.
    assert(result.phase1_iterations > 0);
    assert(result.phase1_iterations + result.phase2_iterations > 0);
    assert(result.phase2_verified);
    assert(result.incumbent_bins == 2);
    assert(result.safe_bound.has_value());
    assert(result.safe_bound->ceil_bins() <= result.incumbent_bins);
#ifdef BPP_HAS_SOPLEX
    assert(result.phase2_backend == "soplex");
#else
    assert(result.phase2_backend == "fixed-gmp");
#endif
  }
  {
    bpp::Instance instance(10, {6, 4, 5, 5});
    bpp::ColumnGenerationOptions options;
    options.max_iterations = 2;
    options.sr3_cuts.push_back(bpp::Sr3Cut(0, 1, 2));
    const auto result = bpp::solve_root_column_generation(instance, options);
    assert(result.incumbent_bins == 2);
  }
  {
    bpp::Instance instance(10, {6, 4, 5, 5});
    bpp::BranchAndPriceOptions options;
    options.max_nodes = 4;
    options.node_options.max_iterations = 20;
    options.node_options.automatic_sr3_separation = true;
    const auto result = bpp::solve_branch_and_price(instance, options);
    assert(result.incumbent_bins == 2);
    assert(result.processed_nodes >= 1);
    // A search that empties its queue is a complete, certified search: the
    // exposed lower bound must be an exact rational equal to the incumbent.
    assert(result.optimal);
    assert(result.lower_bound.has_value());
    assert(result.lower_bound->ceil_bins() == result.incumbent_bins);
  }
  {
    // NodeStrategy::DepthFirst must reach the same certified answer as
    // NodeStrategy::BestBound on the same instance/options: both are exact,
    // they only differ in traversal order and in whether column generation
    // is warm-started from a pool shared across the tree.
    bpp::Instance instance(10, {6, 4, 5, 5});
    bpp::BranchAndPriceOptions options;
    options.max_nodes = 4;
    options.node_options.max_iterations = 20;
    options.node_options.automatic_sr3_separation = true;
    options.node_strategy = bpp::NodeStrategy::DepthFirst;
    const auto result = bpp::solve_branch_and_price(instance, options);
    assert(result.incumbent_bins == 2);
    assert(result.processed_nodes >= 1);
    assert(result.optimal);
    assert(result.lower_bound.has_value());
    assert(result.lower_bound->ceil_bins() == result.incumbent_bins);
  }
  {
    // A slightly larger, asymmetric instance on the depth-first driver,
    // with a node budget generous enough to actually branch (rather than
    // resolve at the root): checks the recursion/backtracking and the
    // warm-start pool plumbing (ColumnGenerationOptions::warm_start_patterns
    // threaded from parent to child) do not corrupt the result.
    bpp::Instance instance(10, {6, 5, 4, 3, 5, 4}, "depth-first-branching");
    bpp::BranchAndPriceOptions options;
    options.max_nodes = 20;
    options.node_options.max_iterations = 50;
    options.node_strategy = bpp::NodeStrategy::DepthFirst;
    const auto result = bpp::solve_branch_and_price(instance, options);
    assert(result.incumbent.is_valid());
    assert(result.processed_nodes >= 1);
    if (result.optimal) {
      assert(result.lower_bound.has_value());
      assert(result.lower_bound->ceil_bins() == result.incumbent_bins);
    }
    // Same instance/options on the best-bound driver must certify the same
    // optimal value if depth-first proved optimality within its budget.
    auto best_bound_options = options;
    best_bound_options.node_strategy = bpp::NodeStrategy::BestBound;
    best_bound_options.max_nodes = 200;
    const auto reference = bpp::solve_branch_and_price(instance, best_bound_options);
    assert(reference.optimal);
    if (result.optimal) assert(result.incumbent_bins == reference.incumbent_bins);
    assert(reference.incumbent_bins <= result.incumbent_bins);
  }
  {
    // Diving is opt-in (BranchAndPriceOptions::diving_enabled) and must
    // never make the final incumbent worse than without it -- it can only
    // find an equal-or-better solution, since solve_branch_and_price only
    // adopts the diving result when it strictly improves incumbent_bins.
    bpp::Instance instance(10, {6, 4, 5, 5});
    bpp::BranchAndPriceOptions options;
    options.max_nodes = 4;
    options.node_options.max_iterations = 20;
    options.diving_enabled = true;
    options.diving_down_budget = 1;
    options.diving_time_limit_seconds = 5.0;
    const auto result = bpp::solve_branch_and_price(instance, options);
    assert(result.incumbent.is_valid());
    assert(result.incumbent_bins == 2);
    assert(result.optimal);
  }
  {
    bpp::Instance instance(10, {6, 4, 5, 5});
    bpp::ColumnGenerationOptions options;
    options.max_iterations = 2;
    options.branching = bpp::BranchingState().child(
        bpp::RyanFosterConstraint(0, 1, bpp::PairRelation::Together));
    options.sr3_cuts.push_back(bpp::Sr3Cut(0, 1, 2));
    const auto result = bpp::solve_root_column_generation(instance, options);
    assert(result.incumbent_bins == 2);
    for (const auto& pattern : result.patterns.patterns()) {
      assert(options.branching.accepts(pattern));
    }
  }
  {
    bpp::Instance instance(10, {6, 4, 5});
    bpp::ColumnGenerationOptions options;
    options.branching = bpp::BranchingState().child(
        bpp::RyanFosterConstraint(0, 1, bpp::PairRelation::Together));
    const auto result = bpp::solve_root_column_generation(instance, options);
    assert(result.converged);
    assert(result.incumbent_bins == 2);
    for (const auto& pattern : result.patterns.patterns()) assert(options.branching.accepts(pattern));
  }
#endif
#ifdef BPP_HAS_SOPLEX
  {
    bpp::Instance instance(10, {6, 4});
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    bpp::SoplexRmp rmp(instance);
    for (const auto& pattern : pool.patterns()) rmp.add_pattern(pattern);
    rmp.solve();
    assert(rmp.objective_value() <= 1.0 + 1e-8);
    assert(rmp.duals().size() == 2);
    assert(rmp.primal_values().size() == 3);
  }
  {
    bpp::Instance instance(12, {6, 4, 5});
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    pool.add({0, 2});
    bpp::SoplexRmp rmp(instance, {bpp::Sr3Cut(0, 1, 2)});
    for (const auto& pattern : pool.patterns()) rmp.add_pattern(pattern);
    rmp.solve();
    assert(rmp.sr3_duals().size() == 1);
    assert(rmp.objective_value() >= 1.0 - 1e-8);
  }
#endif
#ifdef BPP_HAS_GUROBI
  {
    // Direct GurobiRmp checks, mirroring the CplexRmp ones above exactly --
    // same instance, same expected values, since the two are alternative
    // implementations of the identical set-covering RMP.
    bpp::Instance instance(10, {6, 4});
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    bpp::GurobiRmp rmp(instance);
    for (const auto& pattern : pool.patterns()) rmp.add_pattern(pattern);
    rmp.solve();
    assert(rmp.pattern_count() == 3);
    assert(rmp.objective_value() <= 1.0 + 1e-8);
    assert(rmp.duals().size() == 2);
    assert(rmp.primal_values().size() == 3);
  }
  {
    bpp::Instance instance(12, {6, 4, 5});
    bpp::GurobiRmp rmp(instance, {bpp::Sr3Cut(0, 1, 2)});
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    pool.add({0, 2});
    for (const auto& pattern : pool.patterns()) rmp.add_pattern(pattern);
    rmp.solve();
    assert(rmp.sr3_cuts().size() == 1);
    assert(rmp.sr3_duals().size() == 1);
    assert(rmp.objective_value() >= 1.0 - 1e-8);
  }
  {
    // Direct GurobiRmp::solve_mip_at_most check, mirroring the CplexRmp one
    // above exactly (including the one-shot-per-Rmp-instance rule): this is
    // the method a Gurobi-only build used to be entirely missing
    // (try_safe_mip_certification returned immediately on any non-CPLEX
    // backend), so an instance needing this exact certification route
    // would silently report as uncertified purely because of which LP
    // backend the build used -- not a wrong answer, but a real
    // completeness gap. This is the regression test for that fix.
    bpp::Instance instance(10, {5, 5, 5}, "gurobi-solve-mip-at-most");
    bpp::Pattern pair(instance, {0, 1});
    bpp::Pattern single(instance, {2});
    {
      bpp::GurobiRmp rmp(instance);
      rmp.add_pattern(pair);
      rmp.add_pattern(single);
      const auto feasible = rmp.solve_mip_at_most(2);
      assert(feasible.has_value());
      assert(feasible->size() == 2);
    }
    {
      bpp::GurobiRmp rmp(instance);
      rmp.add_pattern(pair);
      rmp.add_pattern(single);
      const auto infeasible = rmp.solve_mip_at_most(0);
      assert(!infeasible.has_value());
    }
  }
  {
    // solve_root_column_generation with LpBackend::Gurobi explicitly
    // selected: this must work standalone in a Gurobi-only build (no
    // BPP_HAS_CPLEX), not just as a cross-check against CPLEX.
    bpp::Instance instance(10, {6, 4, 5, 5});
    bpp::ColumnGenerationOptions gurobi_options;
    gurobi_options.backend = bpp::LpBackend::Gurobi;
    const auto gurobi_result = bpp::solve_root_column_generation(instance, gurobi_options);
    assert(gurobi_result.converged);
    assert(std::abs(gurobi_result.lp_bound - 2.0) < 1e-8);
    assert(gurobi_result.incumbent_bins == 2);
    assert(gurobi_result.safe_duals_feasible);
    assert(gurobi_result.safe_bound.has_value());
    assert(gurobi_result.safe_bound->ceil_bins() <= gurobi_result.incumbent_bins);
  }
#ifdef BPP_HAS_CPLEX
  {
    // Cross-backend equivalence: CPLEX and Gurobi must reach the same
    // certified answer on the same real-shaped instance (automatic SR3
    // separation active) -- this is the check that only runs when the
    // complete suite (both backends) is built, per README.md.
    bpp::Instance instance(20, {9, 8, 7, 6, 5, 4, 3}, "cplex-gurobi-equivalence");
    bpp::ColumnGenerationOptions cplex_options;
    cplex_options.backend = bpp::LpBackend::Cplex;
    cplex_options.automatic_sr3_separation = true;
    const auto cplex_result = bpp::solve_root_column_generation(instance, cplex_options);

    bpp::ColumnGenerationOptions gurobi_options = cplex_options;
    gurobi_options.backend = bpp::LpBackend::Gurobi;
    const auto gurobi_result = bpp::solve_root_column_generation(instance, gurobi_options);

    assert(cplex_result.converged && gurobi_result.converged);
    assert(cplex_result.incumbent_bins == gurobi_result.incumbent_bins);
    // With automatic SR3 separation active, CPLEX and Gurobi can take
    // slightly different separation paths on a small, likely-degenerate
    // instance (same number of cuts added -- verified -- but not
    // necessarily the same specific cuts/columns, due to each solver's own
    // internal tie-breaking), so the raw floating lp_bound is not
    // guaranteed to match exactly between backends. What must match, and
    // does, is the certified integer answer.
    assert(cplex_result.sr3_cuts_added == gurobi_result.sr3_cuts_added);
    assert(cplex_result.safe_bound.has_value() && gurobi_result.safe_bound.has_value());
    assert(cplex_result.safe_bound->ceil_bins() == gurobi_result.safe_bound->ceil_bins());
  }
#endif
#endif
  return 0;
}
