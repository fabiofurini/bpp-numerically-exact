#include "bpp/preprocessing.hpp"

#include <stdexcept>
#include <utility>

namespace bpp {

PreprocessingResult preprocess_forced_singletons(const Instance& instance) {
  std::vector<int> residual_weights;
  std::vector<int> residual_item_ids;
  std::vector<Bin> fixed_bins;
  residual_weights.reserve(instance.item_count());
  residual_item_ids.reserve(instance.item_count());

  for (std::size_t item = 0; item < instance.item_count(); ++item) {
    bool has_compatible_item = false;
    for (std::size_t other = 0; other < instance.item_count(); ++other) {
      if (item == other) continue;
      if (instance.weights()[item] + instance.weights()[other] <= instance.capacity()) {
        has_compatible_item = true;
        break;
      }
    }

    if (has_compatible_item) {
      residual_weights.push_back(instance.weights()[item]);
      residual_item_ids.push_back(static_cast<int>(item));
    } else {
      fixed_bins.push_back(Bin{{static_cast<int>(item)}, instance.weights()[item]});
    }
  }

  return {Instance(instance.capacity(), std::move(residual_weights), instance.name()),
          std::move(residual_item_ids), std::move(fixed_bins)};
}

Solution PreprocessingResult::reconstruct(const Instance& original, const Solution& residual) const {
  std::string error;
  if (!residual.is_valid(&error)) {
    throw std::invalid_argument("cannot reconstruct from invalid residual solution: " + error);
  }
  if (residual_item_ids.size() != residual_instance.item_count()) {
    throw std::invalid_argument("preprocessing result has an inconsistent item mapping");
  }

  Solution complete(&original);
  for (const Bin& fixed_bin : fixed_bins) complete.add_bin(fixed_bin.items);
  for (const Bin& residual_bin : residual.bins()) {
    std::vector<int> original_items;
    original_items.reserve(residual_bin.items.size());
    for (int item : residual_bin.items) {
      if (item < 0 || static_cast<std::size_t>(item) >= residual_item_ids.size()) {
        throw std::invalid_argument("residual solution contains an item outside the mapping");
      }
      original_items.push_back(residual_item_ids[static_cast<std::size_t>(item)]);
    }
    complete.add_bin(std::move(original_items));
  }

  if (!complete.is_valid(&error)) {
    throw std::invalid_argument("preprocessing reconstruction is invalid: " + error);
  }
  return complete;
}

}  // namespace bpp
