#pragma once

#include <cstddef>
#include <stdexcept>

namespace bpp {

struct SolverConfig {
  double time_limit_seconds = 3600.0;
  std::size_t max_column_generation_iterations = 10000;
  double reduced_cost_tolerance = 1e-9;
  bool populate = false;
  bool safe_certification = true;

  void validate() const {
    if (!(time_limit_seconds > 0.0)) throw std::invalid_argument("time limit must be positive");
    if (max_column_generation_iterations == 0) throw std::invalid_argument("iteration limit must be positive");
    if (!(reduced_cost_tolerance >= 0.0)) throw std::invalid_argument("reduced-cost tolerance must be non-negative");
  }
};

}  // namespace bpp
