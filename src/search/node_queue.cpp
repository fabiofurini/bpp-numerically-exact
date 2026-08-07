#include "bpp/search.hpp"

#include <stdexcept>

namespace bpp {

bool BestBoundQueue::Compare::operator()(const SearchNode& left, const SearchNode& right) const {
  const auto left_bound = left.safe_bound.ceil_bins();
  const auto right_bound = right.safe_bound.ceil_bins();
  if (left_bound != right_bound) return left_bound > right_bound;
  if (left.depth != right.depth) return left.depth > right.depth;
  return left.sequence > right.sequence;
}

void BestBoundQueue::push(SearchNode node) { nodes_.push(std::move(node)); }

SearchNode BestBoundQueue::pop() {
  if (nodes_.empty()) throw std::out_of_range("cannot pop an empty search queue");
  SearchNode node = nodes_.top();
  nodes_.pop();
  return node;
}

const SearchNode& BestBoundQueue::peek() const {
  if (nodes_.empty()) throw std::out_of_range("cannot peek an empty search queue");
  return nodes_.top();
}

bool can_prune(const SafeBound& safe_bound, std::int64_t incumbent_bins) {
  if (incumbent_bins < 0) throw std::invalid_argument("incumbent bin count cannot be negative");
  return safe_bound.ceil_bins() >= incumbent_bins;
}

}  // namespace bpp
