#include "bpp/branch_and_price.hpp"

#include "bpp/heuristics.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace bpp {

namespace {

// Historical root-only diving heuristic (BPPS_BP_DIVING.cpp, entry
// condition `level==0 && PARAM_TOKEN_DIV>-1` at BPPS_BP_TREE.cpp:3565): a
// bounded depth-first sub-search that always dives into the Ryan-Foster
// Together branch for whichever fractional pair the LP relaxation already
// leans towards (select_most_confirmed_pair, the historical "IJOC" rule),
// and only ever keeps `down_budget_` Different branches open at once
// (legacy default PARAM_TOKEN_DIV=1: exactly one detour is allowed
// anywhere in the whole dive). Column generation at each diving node is a
// fresh two-phase solve, same as both tree drivers; legacy instead
// re-enters the same persistent-master `master_solve_lp` used by the rest
// of branch-and-price, so per-node cost is not directly comparable, only
// the traversal/branching rule is reproduced faithfully.
class DivingDriver {
 public:
  DivingDriver(const Instance& instance, const BranchAndPriceOptions& options,
              BranchAndPriceResult& result)
      : instance_(instance), options_(options), result_(result),
        deadline_(std::chrono::steady_clock::now() +
                  std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      std::chrono::duration<double>(options.diving_time_limit_seconds))),
        down_budget_(static_cast<long long>(options.diving_down_budget)) {}

  void run(const BranchingState& branching) { dive(branching); }

 private:
  void dive(const BranchingState& branching) {
    if (std::chrono::steady_clock::now() >= deadline_) return;
    ColumnGenerationOptions node_options = options_.node_options;
    node_options.branching = branching;
    const auto relaxation = solve_two_phase_root_column_generation(instance_, node_options);
    if (relaxation.incumbent_bins < result_.incumbent_bins && relaxation.incumbent.is_valid()) {
      result_.incumbent = relaxation.incumbent;
      result_.incumbent_bins = relaxation.incumbent_bins;
    }
    if (!relaxation.converged || !relaxation.safe_bound.has_value()) return;
    if (can_prune(*relaxation.safe_bound, result_.incumbent_bins)) return;
    const auto pair = select_most_confirmed_pair(instance_, relaxation.patterns.patterns(),
                                                 relaxation.master_values, options_.branching_tolerance);
    if (pair.first < 0) return;  // integral: already captured as an incumbent above if it improved
    dive(branching.child(RyanFosterConstraint(pair.first, pair.second, PairRelation::Together)));
    if (down_budget_ > 0) {
      --down_budget_;
      dive(branching.child(RyanFosterConstraint(pair.first, pair.second, PairRelation::Different)));
      ++down_budget_;
    }
  }

  const Instance& instance_;
  const BranchAndPriceOptions& options_;
  BranchAndPriceResult& result_;
  std::chrono::steady_clock::time_point deadline_;
  long long down_budget_;
};

// NodeStrategy::BestBound: a textbook priority-queue branch-and-bound.
// Every node solves column generation from scratch (just the
// singleton/branch-seed patterns); the queue always pops the pending node
// with the smallest certified parent bound, which keeps the queue itself
// cheap to maintain and gives a meaningful partial lower bound (the bound
// of whatever is left in the queue) if the search is stopped early by the
// node limit.
BranchAndPriceResult solve_branch_and_price_best_bound(
    const Instance& instance, const BranchAndPriceOptions& options) {
  BranchAndPriceResult result(instance);
  result.incumbent = solve_best_fit_decreasing(instance);
  result.incumbent_bins = result.incumbent.bin_count();
  BestBoundQueue queue;
  std::size_t sequence = 0;

  auto root_options = options.node_options;
  root_options.branching = BranchingState();
  const auto root = solve_two_phase_root_column_generation(instance, root_options);
  result.processed_nodes = 1;
  if (root.incumbent_bins < result.incumbent_bins && root.incumbent.is_valid()) {
    result.incumbent = root.incumbent;
    result.incumbent_bins = root.incumbent_bins;
  }
  if (!root.converged || !root.safe_bound.has_value()) {
    return result;
  }
  if (can_prune(*root.safe_bound, result.incumbent_bins)) {
    result.pruned_nodes = 1;
    result.optimal = true;
    result.lower_bound = SafeBound(result.incumbent_bins, 1);
    return result;
  }

  const auto root_pair = select_fractional_pair(instance, root.patterns.patterns(),
                                                root.master_values, options.branching_tolerance);
  if (root_pair.first < 0) {
    result.optimal = true;
    result.lower_bound = SafeBound(result.incumbent_bins, 1);
    return result;
  }
  const auto root_together = BranchingState().child(
      RyanFosterConstraint(root_pair.first, root_pair.second, PairRelation::Together));
  const auto root_different = BranchingState().child(
      RyanFosterConstraint(root_pair.first, root_pair.second, PairRelation::Different));
  queue.push({root_together, *root.safe_bound, 1, sequence++});
  queue.push({root_different, *root.safe_bound, 1, sequence++});
  result.generated_nodes += 2;

  while (!queue.empty() && result.processed_nodes < options.max_nodes) {
    const auto node = queue.pop();
    auto node_options = options.node_options;
    node_options.branching = node.branching;
    const auto relaxation = solve_two_phase_root_column_generation(instance, node_options);
    ++result.processed_nodes;
    if (relaxation.incumbent_bins < result.incumbent_bins && relaxation.incumbent.is_valid()) {
      result.incumbent = relaxation.incumbent;
      result.incumbent_bins = relaxation.incumbent_bins;
    }
    if (!relaxation.converged || !relaxation.safe_bound.has_value()) continue;
    if (can_prune(*relaxation.safe_bound, result.incumbent_bins)) {
      ++result.pruned_nodes;
      continue;
    }
    const auto pair = select_fractional_pair(instance, relaxation.patterns.patterns(),
                                             relaxation.master_values, options.branching_tolerance);
    if (pair.first < 0) {
      // Fathomed by integrality: the relaxation is already an integer
      // solution, so this node needs no further branching.
      ++result.pruned_nodes;
      continue;
    }
    const auto together = node.branching.child(
        RyanFosterConstraint(pair.first, pair.second, PairRelation::Together));
    const auto different = node.branching.child(
        RyanFosterConstraint(pair.first, pair.second, PairRelation::Different));
    // The current node bound is a valid lower bound for descendants. Using it
    // keeps the queue cheap while the child relaxation is solved only once
    // when it is popped.
    queue.push({together, *relaxation.safe_bound, node.depth + 1, sequence++});
    queue.push({different, *relaxation.safe_bound, node.depth + 1, sequence++});
    result.generated_nodes += 2;
  }
  // The queue emptying out, regardless of how it compares to the node
  // budget, is what proves every node was fathomed or resolved; a search
  // that happens to hit the node limit on the exact iteration the queue
  // empties is still a complete, certified search.
  result.optimal = queue.empty();
  result.lower_bound = result.optimal ? SafeBound(result.incumbent_bins, 1)
                                      : queue.peek().safe_bound;
  return result;
}

// NodeStrategy::DepthFirst: recursive, Together-branch-first, matching the
// historical tree's traversal order (BPPS_BP_TREE.cpp always recurses into
// the Together child before the Different child, with explicit
// backtracking on return). `pool` accumulates the item-set of every pattern
// priced anywhere in the tree so far — a std::set keeps it deduplicated and
// cheaply searchable, and works because Pattern::items() is always kept
// sorted, so two equal patterns always compare equal as vectors. Before
// solving a node, its column generation is seeded with whichever pooled
// patterns are still feasible under that node's own Ryan-Foster
// constraints (ColumnGenerationOptions::warm_start_patterns), so nodes deep
// in the tree do not re-price the same columns already found by an
// ancestor or an earlier sibling — this is the practical stand-in for the
// historical persistent-master warm start, without needing to replicate its
// row/bound-toggling machinery (see NodeStrategy::DepthFirst's doc comment
// in branch_and_price.hpp for the trade-off).
class DepthFirstDriver {
 public:
  DepthFirstDriver(const Instance& instance, const BranchAndPriceOptions& options,
                   BranchAndPriceResult& result)
      : instance_(instance), options_(options), result_(result) {}

  void run() { visit(BranchingState()); }

  // False if the search stopped before every node was fathomed or resolved
  // (node limit hit, or some node's column generation failed to converge).
  bool complete() const { return complete_; }

 private:
  void remember(const PatternPool& patterns) {
    for (const auto& pattern : patterns.patterns()) pool_.insert(pattern.items());
  }

  void visit(const BranchingState& branching) {
    if (result_.processed_nodes >= options_.max_nodes) {
      complete_ = false;
      return;
    }
    ColumnGenerationOptions node_options = options_.node_options;
    node_options.branching = branching;
    node_options.warm_start_patterns.assign(pool_.begin(), pool_.end());
    const auto relaxation = solve_two_phase_root_column_generation(instance_, node_options);
    ++result_.processed_nodes;
    remember(relaxation.patterns);
    if (relaxation.incumbent_bins < result_.incumbent_bins && relaxation.incumbent.is_valid()) {
      result_.incumbent = relaxation.incumbent;
      result_.incumbent_bins = relaxation.incumbent_bins;
    }
    if (!relaxation.converged || !relaxation.safe_bound.has_value()) {
      // No certified bound for this subtree: the overall search can no
      // longer claim to have proven optimality, even though sibling
      // subtrees may still be explored for incumbent-finding purposes.
      complete_ = false;
      return;
    }
    if (can_prune(*relaxation.safe_bound, result_.incumbent_bins)) {
      ++result_.pruned_nodes;
      return;
    }
    const auto pair = select_fractional_pair(instance_, relaxation.patterns.patterns(),
                                             relaxation.master_values, options_.branching_tolerance);
    if (pair.first < 0) {
      // Fathomed by integrality, same as the best-bound driver.
      ++result_.pruned_nodes;
      return;
    }
    result_.generated_nodes += 2;
    visit(branching.child(RyanFosterConstraint(pair.first, pair.second, PairRelation::Together)));
    if (result_.processed_nodes >= options_.max_nodes) {
      complete_ = false;
      return;
    }
    visit(branching.child(RyanFosterConstraint(pair.first, pair.second, PairRelation::Different)));
  }

  const Instance& instance_;
  const BranchAndPriceOptions& options_;
  BranchAndPriceResult& result_;
  std::set<std::vector<int>> pool_;
  bool complete_ = true;
};

BranchAndPriceResult solve_branch_and_price_depth_first(
    const Instance& instance, const BranchAndPriceOptions& options) {
  BranchAndPriceResult result(instance);
  result.incumbent = solve_best_fit_decreasing(instance);
  result.incumbent_bins = result.incumbent.bin_count();
  DepthFirstDriver driver(instance, options, result);
  driver.run();
  result.optimal = driver.complete();
  // Unlike the best-bound driver, an incomplete depth-first run has no
  // ordered queue to read a partial certified bound from (see the
  // BranchAndPriceResult::lower_bound field comment), so lower_bound stays
  // unset unless the search proved optimality outright.
  if (result.optimal) result.lower_bound = SafeBound(result.incumbent_bins, 1);
  return result;
}

}  // namespace

BranchAndPriceResult solve_branch_and_price(
    const Instance& instance, const BranchAndPriceOptions& options) {
  if (options.max_nodes == 0) throw std::invalid_argument("branch-and-price node limit must be positive");
  if (!std::isfinite(options.branching_tolerance) || options.branching_tolerance < 0.0) {
    throw std::invalid_argument("branching tolerance must be finite and non-negative");
  }
  if (options.diving_down_budget == 0) {
    throw std::invalid_argument("diving down-branch budget must be positive");
  }
  if (!std::isfinite(options.diving_time_limit_seconds) || options.diving_time_limit_seconds <= 0.0) {
    throw std::invalid_argument("diving time limit must be finite and positive");
  }

  BranchAndPriceResult result = [&] {
    switch (options.node_strategy) {
      case NodeStrategy::BestBound:
        return solve_branch_and_price_best_bound(instance, options);
      case NodeStrategy::DepthFirst:
        return solve_branch_and_price_depth_first(instance, options);
    }
    throw std::logic_error("unhandled NodeStrategy");
  }();

  // Diving runs after the main search, seeded with whatever incumbent it
  // already found, rather than before it the way legacy interleaves diving
  // with the very first (root) node: this keeps the two search mechanisms
  // independent and simple to reason about, at the cost of not giving the
  // main search the extra pruning a diving incumbent found *first* would
  // have provided. See DivingDriver's comment for the rest of the trade-off.
  if (options.diving_enabled) {
    BranchAndPriceResult diving_result(instance);
    diving_result.incumbent = result.incumbent;
    diving_result.incumbent_bins = result.incumbent_bins;
    DivingDriver(instance, options, diving_result).run(BranchingState());
    if (diving_result.incumbent_bins < result.incumbent_bins) {
      result.incumbent = diving_result.incumbent;
      result.incumbent_bins = diving_result.incumbent_bins;
    }
  }
  return result;
}

}  // namespace bpp
