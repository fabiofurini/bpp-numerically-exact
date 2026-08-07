#include "bpp/instance.hpp"

#include <numeric>
#include <stdexcept>

namespace bpp {

Instance::Instance(int capacity, std::vector<int> weights, std::string name)
    : capacity_(capacity), weights_(std::move(weights)), name_(std::move(name)) {
  if (capacity_ <= 0) throw std::invalid_argument("bin capacity must be positive");
  for (int weight : weights_) {
    if (weight <= 0) throw std::invalid_argument("item weights must be positive");
    if (weight > capacity_) throw std::invalid_argument("an item is larger than the bin capacity");
  }
}

int Instance::total_weight() const noexcept {
  return std::accumulate(weights_.begin(), weights_.end(), 0);
}

}  // namespace bpp
