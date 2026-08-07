#pragma once

#include "bpp/pattern.hpp"

#include <vector>

namespace bpp {

struct Sr3Cut {
  int first = -1;
  int second = -1;
  int third = -1;
  double dual = 0.0;

  Sr3Cut(int first_item, int second_item, int third_item, double cut_dual = 0.0);
  int coefficient(const Pattern& pattern) const noexcept;
};

struct Sr3Violation {
  Sr3Cut cut;
  double lhs = 0.0;
  double violation = 0.0;
};

// Separates the historical SR3 row family for a restricted-master solution:
// a pattern contributes one when it contains at least two members of a
// triplet, and the row right-hand side is one.
std::vector<Sr3Violation> separate_sr3(const Instance& instance,
                                       const std::vector<Pattern>& patterns,
                                       const std::vector<double>& column_values,
                                       std::size_t max_cuts = 1,
                                       double tolerance = 1e-9);

}  // namespace bpp
