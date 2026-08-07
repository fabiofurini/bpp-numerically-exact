// Unit suite: isolated-component tests that never require CPLEX/SoPlex --
// they exercise one module at a time (Instance/Pattern, the pricers, SafeBound
// arithmetic, preprocessing, branching primitives) with no restricted-master
// solve involved. Always built and run, in both the portable and
// CPLEX-enabled configurations. See tests/test_integration.cpp for
// multi-component solve_* pipeline tests and tests/test_regression.cpp for
// tests pinned to specific previously-fixed bugs.
#include "bpp/branching.hpp"
#include "bpp/cuts.hpp"
#include "bpp/config.hpp"
#include "bpp/safe_bound.hpp"
#include "bpp/search.hpp"
#include "bpp/heuristics.hpp"
#include "bpp/pattern.hpp"
#include "bpp/pricing.hpp"
#include "bpp/preprocessing.hpp"
#include "bpp/solver.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

int main() {
  {
    bpp::Instance instance(10, {6, 4, 5, 5}, "toy");
    auto solution = bpp::solve_greedy(instance);
    assert(solution.is_valid());
    assert(solution.bin_count() == 2);
  }

  {
    bpp::SolverConfig config;
    config.validate();
    assert(!config.populate);
    config.populate = true;
  }

  {
    bpp::Instance instance(10, {6, 4, 5, 5});
    const auto ffd = bpp::solve_first_fit_decreasing(instance);
    const auto bfd = bpp::solve_best_fit_decreasing(instance);
    assert(ffd.is_valid() && bfd.is_valid());
    assert(ffd.bin_count() == 2 && bfd.bin_count() == 2);
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    pool.add({2, 3});
    const auto rounded = bpp::round_master_solution(instance, pool.patterns(), {0.0, 0.0, 0.0, 0.0, 1.0, 1.0});
    assert(rounded.is_valid());
    assert(rounded.bin_count() == 2);
  }

#ifdef BPP_HAS_GMP
  {
    const auto bound = bpp::SafeBound::from_scaled_duals({65, 35}, 100);
    assert(bound.str() == "1");
    assert(bound.ceil_bins() == 1);
    const bpp::SafeBound larger(3, 2);
    assert(larger.ceil_bins() == 2);
    assert(bound < larger);

    bpp::BestBoundQueue queue;
    queue.push({bpp::BranchingState(), larger, 2, 1});
    queue.push({bpp::BranchingState(), bound, 1, 2});
    assert(queue.pop().safe_bound.str() == "1");
    assert(bpp::can_prune(larger, 2));
    assert(!bpp::can_prune(bound, 2));
    const auto conservative = bpp::SafeBound::from_floating_duals({0.651, 0.351}, 100);
    assert(conservative.ceil_bins() == 1);
    bpp::Instance instance(10, {6, 4});
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    pool.add({0, 1});
    const auto certified = bpp::certify_scaled_duals(pool.patterns(), {0.5, 0.5}, 100);
    assert(certified.has_value() && certified->ceil_bins() == 1);
    const auto rejected = bpp::certify_scaled_duals(pool.patterns(), {0.7, 0.7}, 100);
    assert(!rejected.has_value());
    const std::vector<bpp::Sr3Cut> cuts{bpp::Sr3Cut(0, 1, 2)};
    const auto certified_with_cut = bpp::certify_scaled_duals(
        pool.patterns(), {0.5, 0.5}, cuts, {-0.1}, 100);
    assert(certified_with_cut.has_value());
    try {
      (void)bpp::certify_scaled_duals(pool.patterns(), {0.5}, 100);
      assert(false);
    } catch (const std::invalid_argument&) {
    }
  }
#endif

  {
    bpp::Instance instance(10, {9, 8, 2}, "forced-singletons");
    const auto reduced = bpp::preprocess_forced_singletons(instance);
    assert(reduced.fixed_bins.size() == 1);
    assert(reduced.fixed_bins.front().items == std::vector<int>{0});
    assert(reduced.residual_item_ids == std::vector<int>({1, 2}));

    const auto residual_solution = bpp::solve_greedy(reduced.residual_instance);
    const auto complete = reduced.reconstruct(instance, residual_solution);
    assert(complete.is_valid());
    assert(complete.bin_count() == 2);
  }

  {
    bpp::Instance instance(10, {6, 4, 3}, "no-forced-singletons");
    const auto reduced = bpp::preprocess_forced_singletons(instance);
    assert(reduced.fixed_bins.empty());
    assert(reduced.residual_item_ids == std::vector<int>({0, 1, 2}));
  }

  {
    bpp::Instance instance(10, {6, 4, 3}, "patterns");
    bpp::PatternPool pool = bpp::PatternPool::with_singletons(instance);
    assert(pool.size() == 3);
    assert(pool.add({1, 0}) == 3);
    assert(pool.at(3).items() == std::vector<int>({0, 1}));
    assert(pool.at(3).load() == 10);
    assert(pool.add({0, 1}) == 3);
    assert(pool.size() == 4);
    try {
      pool.add({0, 0});
      assert(false);
    } catch (const std::invalid_argument&) {
    }
    try {
      pool.add({0, 2, 1});
      assert(false);
    } catch (const std::invalid_argument&) {
    }
  }

  {
    bpp::Instance instance(10, {6, 4, 7}, "pricing");
    const bpp::FloatingRootPricer pricer;
    const auto result = pricer.price(instance, {0.7, 0.6, 0.9});
    assert(result.pattern.has_value());
    assert(result.pattern->items() == std::vector<int>({0, 1}));
    assert(std::abs(result.dual_value - 1.3) < 1e-12);
    assert(std::abs(result.reduced_cost + 0.3) < 1e-12);
    const auto candidates = pricer.price_candidates(instance, {0.7, 0.6, 0.9}, 8);
    assert(!candidates.empty());
    assert(candidates.front().pattern.has_value());
    assert(candidates.front().reduced_cost <= result.reduced_cost + 1e-12);
    const auto labels = pricer.price_label_setting(instance, {0.7, 0.6, 0.9}, 8);
    assert(!labels.empty());
    assert(labels.front().reduced_cost <= result.reduced_cost + 1e-12);

    const auto no_column = pricer.price(instance, {-1.0, -2.0, -3.0});
    assert(!no_column.pattern.has_value());
    assert(no_column.reduced_cost == 1.0);

    const std::vector<bpp::Sr3Cut> cuts{bpp::Sr3Cut(0, 1, 2, -0.5)};
    const auto sr3_result = pricer.price_with_sr3(instance, {0.6, 0.6, 0.6}, cuts);
    assert(sr3_result.pattern.has_value());
    assert(sr3_result.pattern->items().size() == 2);
    assert(std::abs(sr3_result.dual_value - 0.7) < 1e-12);

    // price_label_setting_with_sr3, like price_label_setting, only returns
    // improving columns (reduced_cost < 0); price_with_sr3 (the DFS
    // reference) unconditionally returns the best pattern found even when it
    // is not improving, which is what happens with this cut's negative dual
    // (dual_value 0.7 < bin_cost 1.0), so the DP correctly returns nothing
    // here. Cross-check the two algorithms instead with a positive-dual cut
    // that does produce an improving column for the same {0,1} pattern.
    assert(sr3_result.reduced_cost >= 0.0);
    const auto dp_not_improving = pricer.price_label_setting_with_sr3(instance, {0.6, 0.6, 0.6}, cuts, 8);
    assert(dp_not_improving.empty());
    const std::vector<bpp::Sr3Cut> improving_cuts{bpp::Sr3Cut(0, 1, 2, 0.5)};
    const auto dfs_improving = pricer.price_with_sr3(instance, {0.6, 0.6, 0.6}, improving_cuts);
    assert(dfs_improving.reduced_cost < 0.0);
    const auto dp_sr3 = pricer.price_label_setting_with_sr3(instance, {0.6, 0.6, 0.6}, improving_cuts, 8);
    assert(!dp_sr3.empty());
    assert(dp_sr3.front().pattern.has_value());
    assert(std::abs(dp_sr3.front().dual_value - dfs_improving.dual_value) < 1e-9);
    assert(std::abs(dp_sr3.front().reduced_cost - dfs_improving.reduced_cost) < 1e-9);

    // With no cuts, the DP falls back to price_label_setting exactly.
    const auto dp_no_cuts = pricer.price_label_setting_with_sr3(instance, {0.7, 0.6, 0.9}, {}, 8);
    assert(!dp_no_cuts.empty());
    assert(std::abs(dp_no_cuts.front().reduced_cost - labels.front().reduced_cost) < 1e-12);
  }

  {
    // Cross-check the DP against the DFS reference on a larger instance with
    // two simultaneously active cuts, including one negative-dual cut (which
    // must never be "cashed in": a negative-dual cut can only hurt value, so
    // an optimal pattern may deliberately avoid completing it).
    bpp::Instance instance(20, {5, 4, 6, 3, 7, 2}, "sr3-dp-cross-check");
    const bpp::FloatingRootPricer pricer;
    const std::vector<double> duals{0.9, 0.8, 0.85, 0.5, 0.95, 0.4};
    const std::vector<bpp::Sr3Cut> cuts{bpp::Sr3Cut(0, 1, 2, 0.6), bpp::Sr3Cut(3, 4, 5, -0.4)};
    const auto dfs_result = pricer.price_with_sr3(instance, duals, cuts);
    const auto dp_result = pricer.price_label_setting_with_sr3(instance, duals, cuts, 16);
    assert(!dp_result.empty());
    assert(std::abs(dp_result.front().dual_value - dfs_result.dual_value) < 1e-9);
    assert(std::abs(dp_result.front().reduced_cost - dfs_result.reduced_cost) < 1e-9);
    // Every returned candidate must be internally consistent: dual_value
    // matches recomputing item duals plus realized cut coefficients.
    for (const auto& candidate : dp_result) {
      assert(candidate.pattern.has_value());
      double expected = 0.0;
      for (int item : candidate.pattern->items()) expected += duals[static_cast<std::size_t>(item)];
      for (const auto& cut : cuts) expected += cut.dual * cut.coefficient(*candidate.pattern);
      assert(std::abs(expected - candidate.dual_value) < 1e-9);
    }
  }

  {
    bpp::Instance instance(10, {6, 4, 7}, "fixed-point-pricing");
    const bpp::FixedPointRootPricer pricer(100);
    const auto result = pricer.price(instance, {70, 60, 90});
    assert(result.pattern.has_value());
    assert(result.pattern->items() == std::vector<int>({0, 1}));
    assert(result.dual_value == 130);
    assert(result.reduced_cost == -30);
  }

  {
    bpp::Instance instance(10, {6, 4, 3}, "branching");
    bpp::Pattern together(instance, {0, 1});
    bpp::Pattern separate(instance, {0, 2});
    bpp::RyanFosterConstraint together_constraint(1, 0, bpp::PairRelation::Together);
    bpp::RyanFosterConstraint different_constraint(0, 2, bpp::PairRelation::Different);
    assert(together_constraint.accepts(together));
    assert(!together_constraint.accepts(separate));
    assert(!different_constraint.accepts(separate));
    auto state = bpp::BranchingState().child(together_constraint);
    assert(state.accepts(together));
    assert(!state.accepts(separate));
    const auto pair = bpp::select_fractional_pair(instance, {together, separate}, {0.5, 0.5});
    assert(pair.first == 0 && pair.second == 1);
    assert(std::abs(pair.together_value - 0.5) < 1e-12);

    const bpp::FloatingRootPricer pricer;
    auto together_state = state;
    const auto together_price = pricer.price(instance, {0.8, 0.8, 0.95}, together_state);
    assert(together_price.pattern.has_value());
    assert(together_price.pattern->items() == std::vector<int>({0, 1}));
    const auto different_state = bpp::BranchingState().child(different_constraint);
    const auto different_price = pricer.price(instance, {0.8, 0.8, 0.95}, different_state);
    assert(different_price.pattern.has_value());
    assert(different_price.pattern->items() != std::vector<int>({0, 2}));
    assert(different_state.accepts(*different_price.pattern));

    // The batched branch-aware DP (price_label_setting_with_branching_and_sr3)
    // must match the DFS reference exactly, with and without SR3 cuts, since
    // it replaces the DFS on the production hot path (run_floating_pricing_loop).
    const auto dp_together = pricer.price_label_setting_with_branching_and_sr3(
        instance, {0.8, 0.8, 0.95}, together_state, {}, 8);
    assert(!dp_together.empty());
    assert(std::abs(dp_together.front().dual_value - together_price.dual_value) < 1e-9);
    assert(dp_together.front().pattern->items() == together_price.pattern->items());

    const auto dp_different = pricer.price_label_setting_with_branching_and_sr3(
        instance, {0.8, 0.8, 0.95}, different_state, {}, 8);
    assert(!dp_different.empty());
    assert(std::abs(dp_different.front().dual_value - different_price.dual_value) < 1e-9);
    assert(different_state.accepts(*dp_different.front().pattern));
    for (const auto& candidate : dp_different) {
      assert(different_state.accepts(*candidate.pattern));
    }

    // With no branching constraints at all, the branch-aware DP must fall
    // back to price_label_setting/price_label_setting_with_sr3 exactly.
    const auto dp_no_branching = pricer.price_label_setting_with_branching_and_sr3(
        instance, {0.8, 0.8, 0.95}, bpp::BranchingState(), {}, 8);
    const auto plain = pricer.price_label_setting(instance, {0.8, 0.8, 0.95}, 8);
    assert(dp_no_branching.size() == plain.size());
    if (!dp_no_branching.empty()) {
      assert(std::abs(dp_no_branching.front().dual_value - plain.front().dual_value) < 1e-12);
    }
  }

  {
    // Branching + SR3 together: a Together constraint that merges two of an
    // SR3 cut's three items into a single element. The cut's coefficient
    // (Sr3Cut::coefficient) is 1 once at least two of its three items are
    // present in the pattern, so selecting this merged element must award
    // the cut's dual bonus in one step even though it adds two triplet items
    // at once -- unlike the item-at-a-time root SR3 DP, which only ever
    // crosses a cut's count by one per extension.
    bpp::Instance instance(20, {5, 4, 6, 3, 7}, "branching-sr3");
    const bpp::FloatingRootPricer pricer;
    const std::vector<double> duals{0.9, 0.85, 0.5, 0.6, 0.4};
    const std::vector<bpp::Sr3Cut> cuts{bpp::Sr3Cut(0, 1, 2, 0.5)};
    // Force items 0 and 1 (two of the cut's three items) together.
    const auto branching = bpp::BranchingState().child(
        bpp::RyanFosterConstraint(0, 1, bpp::PairRelation::Together));

    const auto dfs_result = pricer.price_with_branching_and_sr3(instance, duals, branching, cuts);
    const auto dp_result = pricer.price_label_setting_with_branching_and_sr3(
        instance, duals, branching, cuts, 16);
    assert(!dp_result.empty());
    assert(std::abs(dp_result.front().dual_value - dfs_result.dual_value) < 1e-9);
    assert(std::abs(dp_result.front().reduced_cost - dfs_result.reduced_cost) < 1e-9);
    assert(branching.accepts(*dp_result.front().pattern));
    // The best pattern must include items 0 and 1 (forced together) and
    // realize the cut bonus (dual_value strictly above the sum of the two
    // items' own duals, since the merged element alone -- without a third
    // triplet member -- would not otherwise reach 2 of 3).
    const auto& best_items = dp_result.front().pattern->items();
    assert(std::find(best_items.begin(), best_items.end(), 0) != best_items.end());
    assert(std::find(best_items.begin(), best_items.end(), 1) != best_items.end());

    // Every returned candidate must be internally consistent: dual_value
    // matches recomputing item duals plus realized cut coefficients, and
    // every candidate respects the branching state.
    for (const auto& candidate : dp_result) {
      assert(candidate.pattern.has_value());
      assert(branching.accepts(*candidate.pattern));
      double expected = 0.0;
      for (int item : candidate.pattern->items()) expected += duals[static_cast<std::size_t>(item)];
      for (const auto& cut : cuts) expected += cut.dual * cut.coefficient(*candidate.pattern);
      assert(std::abs(expected - candidate.dual_value) < 1e-9);
    }
  }

  {
    // Larger cross-check: multiple Together/Different constraints plus two
    // simultaneously active SR3 cuts (one negative-dual), comparing the
    // batched DP against the DFS reference on a bigger item set.
    bpp::Instance instance(25, {5, 4, 6, 3, 7, 2, 4}, "branching-sr3-cross-check");
    const bpp::FloatingRootPricer pricer;
    const std::vector<double> duals{0.9, 0.8, 0.85, 0.5, 0.95, 0.4, 0.6};
    const std::vector<bpp::Sr3Cut> cuts{bpp::Sr3Cut(0, 1, 2, 0.6), bpp::Sr3Cut(3, 4, 5, -0.4)};
    const auto branching = bpp::BranchingState()
                               .child(bpp::RyanFosterConstraint(2, 3, bpp::PairRelation::Together))
                               .child(bpp::RyanFosterConstraint(4, 6, bpp::PairRelation::Different));

    const auto dfs_result = pricer.price_with_branching_and_sr3(instance, duals, branching, cuts);
    const auto dp_result = pricer.price_label_setting_with_branching_and_sr3(
        instance, duals, branching, cuts, 16);
    assert(!dp_result.empty());
    assert(std::abs(dp_result.front().dual_value - dfs_result.dual_value) < 1e-9);
    assert(std::abs(dp_result.front().reduced_cost - dfs_result.reduced_cost) < 1e-9);
    for (const auto& candidate : dp_result) {
      assert(candidate.pattern.has_value());
      assert(branching.accepts(*candidate.pattern));
      double expected = 0.0;
      for (int item : candidate.pattern->items()) expected += duals[static_cast<std::size_t>(item)];
      for (const auto& cut : cuts) expected += cut.dual * cut.coefficient(*candidate.pattern);
      assert(std::abs(expected - candidate.dual_value) < 1e-9);
    }
  }

  {
    bpp::Instance instance(10, {4, 3, 3, 2}, "sr3");
    bpp::Pattern pair(instance, {0, 1});
    bpp::Pattern other_pair(instance, {2, 3});
    const auto violations = bpp::separate_sr3(instance, {pair, other_pair}, {1.1, 1.1}, 1);
    assert(violations.size() == 1);
    assert(violations.front().cut.first == 0);
    assert(violations.front().violation > 0.0);
    assert(violations.front().cut.coefficient(pair) == 1);
  }

  {
    // select_most_confirmed_pair (the "IJOC" diving rule) must pick the
    // fractional pair closest to 1, not the pair closest to 0.5 that
    // select_fractional_pair (tree branching) would pick. Three pair
    // columns give pair (0,1) value 0.9 (fractional, near 1) and pair (2,3)
    // value 0.5 (fractional, maximally uncertain); diving must prefer (0,1).
    bpp::Instance instance(10, {4, 3, 3, 2}, "diving-ijoc");
    bpp::Pattern near_integral(instance, {0, 1});
    bpp::Pattern maximally_fractional(instance, {2, 3});
    const auto diving_pick = bpp::select_most_confirmed_pair(
        instance, {near_integral, maximally_fractional}, {0.9, 0.5});
    assert(diving_pick.first == 0 && diving_pick.second == 1);
    const auto tree_pick = bpp::select_fractional_pair(
        instance, {near_integral, maximally_fractional}, {0.9, 0.5});
    assert(tree_pick.first == 2 && tree_pick.second == 3);
  }

  {
    // Legacy tie-break (BPPS_BP_TREE.cpp:314): among equally fractional
    // pairs, prefer the one with the larger combined weight. Pair (0,1) is
    // enumerated first (weights 3+4=7) but pair (2,3) (weights 6+5=11) must
    // still win, proving the tie-break overrides enumeration order rather
    // than just keeping the first pair found.
    bpp::Instance instance(20, {3, 4, 6, 5}, "branch-tiebreak");
    bpp::Pattern first_pair(instance, {0, 1});
    bpp::Pattern second_pair(instance, {2, 3});
    const auto pair = bpp::select_fractional_pair(instance, {first_pair, second_pair}, {0.5, 0.5});
    assert(pair.first == 2);
    assert(pair.second == 3);
  }

  return 0;
}
