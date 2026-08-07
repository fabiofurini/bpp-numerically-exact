#pragma once

#include "bpp/branching.hpp"
#include "bpp/safe_bound.hpp"

#include <cstddef>
#include <functional>
#include <queue>
#include <vector>

namespace bpp {

struct SearchNode {
  BranchingState branching;
  SafeBound safe_bound;
  std::size_t depth = 0;
  std::size_t sequence = 0;
};

class BestBoundQueue {
 public:
  void push(SearchNode node);
  SearchNode pop();
  // The node that would be returned by the next pop(), without removing it.
  // Its safe bound is the current global lower bound over every unprocessed
  // node, since the queue always pops the smallest bound first.
  const SearchNode& peek() const;
  bool empty() const noexcept { return nodes_.empty(); }
  std::size_t size() const noexcept { return nodes_.size(); }

 private:
  struct Compare {
    bool operator()(const SearchNode& left, const SearchNode& right) const;
  };
  std::priority_queue<SearchNode, std::vector<SearchNode>, Compare> nodes_;
};

// With a minimisation objective, a node whose safe lower bound rounds up to
// the incumbent cannot contain a better integer solution.
bool can_prune(const SafeBound& safe_bound, std::int64_t incumbent_bins);

}  // namespace bpp
