#include "bpp/pattern.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace bpp {

Pattern::Pattern(const Instance& instance, std::vector<int> items) : items_(std::move(items)) {
  if (items_.empty()) throw std::invalid_argument("a pattern must contain at least one item");
  std::sort(items_.begin(), items_.end());
  if (std::adjacent_find(items_.begin(), items_.end()) != items_.end()) {
    throw std::invalid_argument("a pattern cannot contain an item more than once");
  }
  for (int item : items_) {
    if (item < 0 || static_cast<std::size_t>(item) >= instance.item_count()) {
      throw std::invalid_argument("pattern item index out of range");
    }
    load_ += instance.weights()[static_cast<std::size_t>(item)];
  }
  if (load_ > instance.capacity()) throw std::invalid_argument("pattern exceeds bin capacity");
}

std::size_t PatternPool::add(std::vector<int> items) {
  Pattern candidate(*instance_, std::move(items));
  const auto insertion = keys_.insert(candidate.items());
  if (!insertion.second) {
    const auto existing = std::find_if(patterns_.begin(), patterns_.end(),
        [&candidate](const Pattern& pattern) { return pattern.items() == candidate.items(); });
    return static_cast<std::size_t>(existing - patterns_.begin());
  }
  patterns_.push_back(std::move(candidate));
  return patterns_.size() - 1;
}

PatternPool PatternPool::with_singletons(const Instance& instance) {
  PatternPool pool(instance);
  for (std::size_t item = 0; item < instance.item_count(); ++item) {
    pool.add({static_cast<int>(item)});
  }
  return pool;
}

}  // namespace bpp
