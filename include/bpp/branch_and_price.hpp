#pragma once

#include "bpp/column_generation.hpp"
#include "bpp/safe_bound.hpp"
#include "bpp/search.hpp"

#include <optional>

namespace bpp {

// How the tree picks which pending node to solve next. Both strategies are
// exact (a complete run of either proves optimality); they only differ in
// exploration order and in how much a node's column generation reuses work
// done elsewhere in the tree.
enum class NodeStrategy {
  // Always solve the node with the best (smallest) certified parent bound
  // first, like a textbook A*/branch-and-bound priority queue. Each node
  // still starts column generation from just the singleton patterns (plus
  // whatever the branch-seed patterns are), so it never reuses columns
  // priced at other nodes. This is the strategy used before depth-first was
  // added and remains the default so existing callers are unaffected.
  BestBound,
  // Depth-first, Together-branch-first, matching the historical tree's
  // traversal order (BPPS_BP_TREE.cpp: the Together child is always
  // recursed into before the Different child, with explicit backtracking).
  // A pool of every pattern ever priced anywhere in the tree is threaded
  // through the recursion; each node seeds its own column generation from
  // the subset of that pool that is feasible under its own Ryan--Foster
  // constraints, instead of starting from just two singleton patterns.
  // This reuses most of the historical warm-start benefit (thousands of
  // already-priced columns are filtered and reused, not rediscovered by
  // pricing) without needing the historical persistent-master
  // row/bound-toggling machinery. See docs/STATUS.md for the design
  // rationale and its known simplification (no partial lower bound is
  // reported when the search stops early; see BranchAndPriceResult below).
  DepthFirst,
};

struct BranchAndPriceOptions {
  ColumnGenerationOptions node_options;
  std::size_t max_nodes = 1000;
  double branching_tolerance = 1e-9;
  NodeStrategy node_strategy = NodeStrategy::BestBound;
  // Historical root-only diving primal heuristic (BPPS_BP_DIVING.cpp,
  // triggered by `level==0 && PARAM_TOKEN_DIV>-1` in BPPS_BP_TREE.cpp:3565
  // -- fires exactly once per solve, right after the root relaxation
  // converges, never per-node). It is a bounded depth-first sub-search that
  // repeatedly fixes the fractional Ryan-Foster pair the LP already leans
  // towards Together (select_most_confirmed_pair, the historical "IJOC"
  // rule) and only ever opens `diving_down_budget` Different branches at
  // once (legacy default PARAM_TOKEN_DIV=1: a single detour). Off by
  // default so existing callers are unaffected; the historical default
  // (param_BPPS_BP_v1.txt/v4.txt) is enabled with a budget of 1.
  bool diving_enabled = false;
  std::size_t diving_down_budget = 1;
  // Legacy PARAM_TL_DIVING (v1.txt/v4.txt default: 400 seconds) -- diving's
  // own wall-clock sub-budget, independent of any overall solve time limit.
  double diving_time_limit_seconds = 400.0;
};

struct BranchAndPriceResult {
  Solution incumbent;
  int incumbent_bins = 0;
  std::size_t processed_nodes = 0;
  std::size_t generated_nodes = 0;
  std::size_t pruned_nodes = 0;
  bool optimal = false;
  // The certified global lower bound over the whole tree: exactly
  // incumbent_bins when optimal is true (every node was fathomed or
  // resolved). With NodeStrategy::BestBound this is also populated when the
  // search stops early (node limit hit), from the best remaining
  // unprocessed node in the priority queue. With NodeStrategy::DepthFirst it
  // is only populated when optimal is true: an incomplete depth-first run
  // does not keep the pending nodes ordered by bound, so there is no single
  // "best remaining" node to report without extra bookkeeping this
  // implementation does not do yet.
  std::optional<SafeBound> lower_bound;

  explicit BranchAndPriceResult(const Instance& instance) : incumbent(&instance) {}
};

// Executes a classical BPP Ryan--Foster branch-and-price search. Each node
// uses the branch-aware column generation engine. options.node_strategy
// selects the traversal order; see NodeStrategy above.
BranchAndPriceResult solve_branch_and_price(
    const Instance& instance, const BranchAndPriceOptions& options = {});

}  // namespace bpp
