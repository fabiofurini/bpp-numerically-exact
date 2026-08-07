#pragma once

#include "bpp/instance.hpp"
#include "bpp/solution.hpp"

#include <vector>

namespace bpp {

// Result of reductions that are guaranteed to preserve the optimum.  Item
// indices in residual_instance are mapped back through residual_item_ids.
struct PreprocessingResult {
  Instance residual_instance;
  std::vector<int> residual_item_ids;
  std::vector<Bin> fixed_bins;

  // Rebuilds a solution for the original instance from a solution of the
  // residual instance. Throws std::invalid_argument if it is not a complete,
  // feasible residual solution.
  Solution reconstruct(const Instance& original, const Solution& residual) const;
};

// Removes items that cannot fit with any other item. Such items are forced to
// occupy a singleton bin in every feasible packing.
PreprocessingResult preprocess_forced_singletons(const Instance& instance);

}  // namespace bpp
