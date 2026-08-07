#include "bpp/instance_reader.hpp"
#include <fstream>
#include <stdexcept>

namespace bpp {
Instance read_instance(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open instance: " + path);
  int n = 0, capacity = 0;
  if (!(input >> n >> capacity) || n < 0) throw std::runtime_error("invalid BPP header; expected: <number_of_items> <capacity>");
  std::vector<int> weights;
  weights.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) { int w; if (!(input >> w)) throw std::runtime_error("not enough item weights"); weights.push_back(w); }
  return Instance(capacity, std::move(weights), path);
}
}  // namespace bpp
