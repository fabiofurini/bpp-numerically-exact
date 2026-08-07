#pragma once

#include "bpp/instance.hpp"
#include <string>
#include <vector>

namespace bpp {

struct Bin {
  std::vector<int> items;
  int load = 0;
};

class Solution {
 public:
  explicit Solution(const Instance* instance = nullptr) : instance_(instance) {}
  void add_bin(std::vector<int> items);
  const std::vector<Bin>& bins() const noexcept { return bins_; }
  int bin_count() const noexcept { return static_cast<int>(bins_.size()); }
  bool is_valid(std::string* error = nullptr) const;

 private:
  const Instance* instance_;
  std::vector<Bin> bins_;
};

}  // namespace bpp
