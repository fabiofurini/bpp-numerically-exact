#pragma once

#include "bpp/instance.hpp"

#include <cstddef>
#include <set>
#include <vector>

namespace bpp {

// A feasible packing pattern. Item IDs are canonicalised in ascending order,
// so equivalent columns have one representation throughout master, pricing and
// search.
class Pattern {
 public:
  Pattern(const Instance& instance, std::vector<int> items);

  const std::vector<int>& items() const noexcept { return items_; }
  int load() const noexcept { return load_; }

 private:
  std::vector<int> items_;
  int load_ = 0;
};

// Owns master columns and suppresses duplicate patterns. Pattern indices are
// stable for the lifetime of the pool.
class PatternPool {
 public:
  explicit PatternPool(const Instance& instance) : instance_(&instance) {}

  std::size_t add(std::vector<int> items);
  std::size_t size() const noexcept { return patterns_.size(); }
  const Pattern& at(std::size_t index) const { return patterns_.at(index); }
  const std::vector<Pattern>& patterns() const noexcept { return patterns_; }

  // Adds one singleton column per item and returns the resulting pool.
  static PatternPool with_singletons(const Instance& instance);

 private:
  const Instance* instance_;
  std::vector<Pattern> patterns_;
  std::set<std::vector<int>> keys_;
};

}  // namespace bpp
