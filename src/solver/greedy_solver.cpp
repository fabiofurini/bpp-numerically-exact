#include "bpp/solver.hpp"
#include <algorithm>

namespace bpp {
Solution solve_greedy(const Instance& instance) {
  std::vector<int> order(instance.item_count());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
  std::sort(order.begin(), order.end(), [&](int a, int b) { return instance.weights()[a] > instance.weights()[b]; });
  Solution solution(&instance);
  std::vector<int> loads;
  std::vector<std::vector<int>> bins;
  for (int item : order) {
    auto it = std::find_if(loads.begin(), loads.end(), [&](int load) { return load + instance.weights()[item] <= instance.capacity(); });
    if (it == loads.end()) { loads.push_back(instance.weights()[item]); bins.push_back({item}); }
    else { const auto index = static_cast<std::size_t>(it - loads.begin()); *it += instance.weights()[item]; bins[index].push_back(item); }
  }
  for (auto& bin : bins) solution.add_bin(std::move(bin));
  return solution;
}
}  // namespace bpp
