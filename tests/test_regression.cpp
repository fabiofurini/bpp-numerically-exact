// Regression suite: each test here is pinned to one specific, previously
// measured bug in this codebase's history (see docs/STATUS.md for the full
// narrative), not general correctness coverage -- the point is to catch a
// silent reintroduction of an already-fixed defect, not to broaden coverage
// of new code. Correctness-only (equivalence against a slower-but-simple
// reference implementation); this suite does not assert on wall-clock time,
// since timing thresholds are flaky in CI and the DFS references used here
// are also the ones the fixes replaced on the hot path -- a performance
// regression would need to be caught by the ANI comparison scripts
// (scripts/run_ani_comparison.sh), not CTest.
// See tests/test_unit.cpp for isolated-component tests and
// tests/test_integration.cpp for multi-component solve_* pipeline tests.
#include "bpp/branching.hpp"
#include "bpp/cuts.hpp"
#include "bpp/pattern.hpp"
#include "bpp/pricing.hpp"
#include <cassert>
#include <cmath>

#ifdef BPP_HAS_CPLEX
#include "bpp/column_generation.hpp"
#endif

int main() {
  {
    // Regression: the pricing DP used to be a dense array of size
    // capacity+1 (price_label_setting/price_label_setting_with_sr3), whose
    // cost scaled directly with capacity rather than with the number of
    // genuinely distinct (load, value) trade-offs -- this is what made
    // ANI-402 (402 items, capacity ~7550) blow up before the sparse
    // label-setting frontier fix (docs/STATUS.md, "ANI-402 fixed"
    // checkpoint). This does not re-test the performance of that fix (see
    // file header), only that the sparse frontier still finds the exact
    // same optimum as an independent full enumeration
    // (FloatingRootPricer::price_candidates, a dense DP kept as reference)
    // on a moderately large-capacity instance.
    bpp::Instance instance(500, {137, 98, 76, 54, 210, 33, 61, 149, 88, 42, 175, 29}, "ani402-shape");
    const bpp::FloatingRootPricer pricer;
    const std::vector<double> duals{0.9, 0.7, 0.55, 0.4, 0.95, 0.2, 0.5, 0.85, 0.6, 0.3, 0.9, 0.15};
    const auto reference = pricer.price_candidates(instance, duals, 64);
    const auto sparse = pricer.price_label_setting(instance, duals, 64);
    assert(!reference.empty());
    assert(!sparse.empty());
    assert(std::abs(reference.front().reduced_cost - sparse.front().reduced_cost) < 1e-9);
    assert(std::abs(reference.front().dual_value - sparse.front().dual_value) < 1e-9);
  }

  {
    // Regression: SR3-aware pricing used to fall back to an exponential-ish
    // DFS (price_with_sr3) whenever any cut was active, which never
    // converged on real ANI-201 instances with automatic SR3 separation
    // (docs/STATUS.md, "SR3 performance fix" checkpoint). Pin the batched
    // label-setting DP (price_label_setting_with_sr3) against that same DFS
    // reference with several simultaneously active cuts, including a
    // negative-dual one that must never be "cashed in".
    bpp::Instance instance(30, {6, 5, 7, 4, 8, 3, 9, 2, 5, 6}, "sr3-multi-cut-regression");
    const bpp::FloatingRootPricer pricer;
    const std::vector<double> duals{0.9, 0.8, 0.7, 0.6, 0.85, 0.5, 0.95, 0.4, 0.75, 0.65};
    const std::vector<bpp::Sr3Cut> cuts{
        bpp::Sr3Cut(0, 1, 2, 0.5), bpp::Sr3Cut(3, 4, 5, 0.3), bpp::Sr3Cut(6, 7, 8, -0.4)};
    const auto dfs_result = pricer.price_with_sr3(instance, duals, cuts);
    const auto dp_result = pricer.price_label_setting_with_sr3(instance, duals, cuts, 32);
    assert(!dp_result.empty());
    assert(std::abs(dp_result.front().dual_value - dfs_result.dual_value) < 1e-9);
    assert(std::abs(dp_result.front().reduced_cost - dfs_result.reduced_cost) < 1e-9);
  }

  {
    // Regression: branch-and-price nodes other than the root used to route
    // through a group-based DFS that priced one column per call and also
    // recomputed every SR3 cut's coefficient from scratch at every
    // recursive call -- confirmed unusable on a real ANI-201 instance
    // needing branching (201_2500_NR_41.txt: a 5-node --branch-price run
    // did not finish in 60s before the fix, see docs/STATUS.md's
    // "branching-node pricer fixed" checkpoint and
    // REPORT_REFACTORING_BPP.tex). Pin the batched branch-aware DP
    // (price_label_setting_with_branching_and_sr3) against the DFS
    // reference on a larger, denser instance than the smoke tests in
    // tests/test_unit.cpp: more items, two Together merges, two Different
    // conflicts, and two simultaneously active SR3 cuts (one of them
    // straddling a Together-merged element, exercising the multi-item-per-
    // extension cut-count generalization the fix required).
    bpp::Instance instance(
        40, {6, 5, 7, 4, 8, 3, 9, 2, 5, 6, 4, 7}, "branching-pricer-regression");
    const bpp::FloatingRootPricer pricer;
    const std::vector<double> duals{0.9, 0.8, 0.7, 0.6, 0.85, 0.5, 0.95, 0.4, 0.75, 0.65, 0.55, 0.6};
    const std::vector<bpp::Sr3Cut> cuts{bpp::Sr3Cut(0, 1, 4, 0.5), bpp::Sr3Cut(6, 7, 8, -0.3)};
    const auto branching = bpp::BranchingState()
                               .child(bpp::RyanFosterConstraint(0, 1, bpp::PairRelation::Together))
                               .child(bpp::RyanFosterConstraint(9, 10, bpp::PairRelation::Together))
                               .child(bpp::RyanFosterConstraint(2, 3, bpp::PairRelation::Different))
                               .child(bpp::RyanFosterConstraint(5, 6, bpp::PairRelation::Different));
    const auto dfs_result = pricer.price_with_branching_and_sr3(instance, duals, branching, cuts);
    const auto dp_result = pricer.price_label_setting_with_branching_and_sr3(
        instance, duals, branching, cuts, 32);
    assert(!dp_result.empty());
    assert(std::abs(dp_result.front().dual_value - dfs_result.dual_value) < 1e-9);
    assert(std::abs(dp_result.front().reduced_cost - dfs_result.reduced_cost) < 1e-9);
    for (const auto& candidate : dp_result) {
      assert(candidate.pattern.has_value());
      assert(branching.accepts(*candidate.pattern));
    }
  }

#ifdef BPP_HAS_CPLEX
  {
    // Regression: dual-value stabilization must never change the certified
    // optimum it converges to, on any instance, regardless of how many
    // misprice/self-deactivation cycles it goes through internally -- it is
    // an alternate trajectory of dual values fed to the pricer, never a
    // change to the master's feasible region or objective. Uses yet another
    // instance from the ones already covered in
    // tests/test_integration.cpp's stabilization block, specifically to
    // keep this an independent pin rather than a duplicate of that test.
    bpp::Instance instance(15, {7, 6, 5, 4, 3, 8, 2, 9}, "stabilization-regression");
    const auto baseline = bpp::solve_root_column_generation(instance);
    bpp::ColumnGenerationOptions stabilized_options;
    stabilized_options.dual_stabilization = true;
    const auto stabilized = bpp::solve_root_column_generation(instance, stabilized_options);
    assert(baseline.converged && stabilized.converged);
    assert(baseline.incumbent_bins == stabilized.incumbent_bins);
    assert(std::abs(baseline.lp_bound - stabilized.lp_bound) < 1e-6);
  }
#endif

  return 0;
}
