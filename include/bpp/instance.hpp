#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bpp {

class Instance {
 public:
  Instance() = default;
  Instance(int capacity, std::vector<int> weights, std::string name = {});

  int capacity() const noexcept { return capacity_; }
  const std::vector<int>& weights() const noexcept { return weights_; }
  std::size_t item_count() const noexcept { return weights_.size(); }
  const std::string& name() const noexcept { return name_; }
  int total_weight() const noexcept;

 private:
  int capacity_ = 0;
  std::vector<int> weights_;
  std::string name_;
};

}  // namespace bpp
