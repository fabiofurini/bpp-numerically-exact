#include "bpp/solution.hpp"

#include <numeric>

namespace bpp {

void Solution::add_bin(std::vector<int> items) {
  Bin bin;
  bin.items = std::move(items);
  if (instance_ != nullptr) {
    for (int item : bin.items) bin.load += instance_->weights().at(static_cast<std::size_t>(item));
  }
  bins_.push_back(std::move(bin));
}

bool Solution::is_valid(std::string* error) const {
  if (instance_ == nullptr) { if (error) *error = "solution has no instance"; return false; }
  std::vector<int> seen(instance_->item_count(), 0);
  for (const auto& bin : bins_) {
    int load = 0;
    for (int item : bin.items) {
      if (item < 0 || static_cast<std::size_t>(item) >= instance_->item_count()) {
        if (error) *error = "item index out of range";
        return false;
      }
      if (++seen[static_cast<std::size_t>(item)] != 1) {
        if (error) *error = "an item is missing or appears more than once";
        return false;
      }
      load += instance_->weights()[static_cast<std::size_t>(item)];
    }
    if (load > instance_->capacity()) { if (error) *error = "bin capacity exceeded"; return false; }
  }
  for (int count : seen) if (count != 1) { if (error) *error = "not all items are packed"; return false; }
  return true;
}

}  // namespace bpp
