#pragma once

#include "bpp/pattern.hpp"
#include "bpp/solution.hpp"

#include <vector>

namespace bpp {

Solution solve_first_fit_decreasing(const Instance& instance);
Solution solve_best_fit_decreasing(const Instance& instance);

// Rounds a restricted-master solution by selecting high-valued compatible
// patterns and packing all uncovered items with best-fit decreasing.
Solution round_master_solution(const Instance& instance,
                               const std::vector<Pattern>& patterns,
                               const std::vector<double>& column_values,
                               double selection_threshold = 0.5);

// A deterministic LP-diving heuristic: repeatedly fixes the compatible
// column with largest fractional value, then completes the residual by BFD.
Solution dive_master_solution(const Instance& instance,
                              const std::vector<Pattern>& patterns,
                              const std::vector<double>& column_values);

}  // namespace bpp
