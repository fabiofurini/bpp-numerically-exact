#include "bpp/cuts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

namespace bpp {

Sr3Cut::Sr3Cut(int first_item, int second_item, int third_item, double cut_dual)
    : first(first_item), second(second_item), third(third_item), dual(cut_dual) {
  if (first < 0 || second < 0 || third < 0 || first == second || first == third || second == third) {
    throw std::invalid_argument("an SR3 cut requires three distinct non-negative items");
  }
  if (!std::isfinite(dual)) throw std::invalid_argument("SR3 dual must be finite");
}

int Sr3Cut::coefficient(const Pattern& pattern) const noexcept {
  int count = 0;
  for (int item : pattern.items()) {
    count += (item == first || item == second || item == third) ? 1 : 0;
  }
  return count >= 2 ? 1 : 0;
}

std::vector<Sr3Violation> separate_sr3(const Instance& instance,
                                       const std::vector<Pattern>& patterns,
                                       const std::vector<double>& column_values,
                                       std::size_t max_cuts,
                                       double tolerance) {
  if (patterns.size() != column_values.size()) {
    throw std::invalid_argument("one master value is required for every pattern");
  }
  if (max_cuts == 0) return {};
  if (tolerance < 0.0 || !std::isfinite(tolerance)) {
    throw std::invalid_argument("SR3 tolerance must be finite and non-negative");
  }

  const int n = static_cast<int>(instance.item_count());
  // For a triplet (i,j,k), the SR3 activity is the sum of the three pair
  // activities minus twice the activity of the full triple.  Accumulating
  // pair/triple activities once per column avoids the historical O(n^3*m)
  // scan and makes automatic separation usable on ANI-402.
  std::vector<double> pair_activity(static_cast<std::size_t>(n) * n, 0.0);
  std::unordered_map<std::uint64_t, double> triple_activity;
  auto triple_key = [n](int first, int second, int third) {
    return (static_cast<std::uint64_t>(first) * static_cast<std::uint64_t>(n) +
            static_cast<std::uint64_t>(second)) * static_cast<std::uint64_t>(n) +
           static_cast<std::uint64_t>(third);
  };
  for (std::size_t column = 0; column < patterns.size(); ++column) {
    const auto& items = patterns[column].items();
    const double value = column_values[column];
    for (std::size_t left = 0; left < items.size(); ++left) {
      for (std::size_t right = left + 1; right < items.size(); ++right) {
        const int first = items[left];
        const int second = items[right];
        pair_activity[static_cast<std::size_t>(first) * n + second] += value;
        pair_activity[static_cast<std::size_t>(second) * n + first] += value;
      }
    }
    for (std::size_t first = 0; first < items.size(); ++first) {
      for (std::size_t second = first + 1; second < items.size(); ++second) {
        for (std::size_t third = second + 1; third < items.size(); ++third) {
          triple_activity[triple_key(items[first], items[second], items[third])] += value;
        }
      }
    }
  }

  std::vector<Sr3Violation> violations;
  for (int first = 0; first < n; ++first) {
    for (int second = first + 1; second < n; ++second) {
      for (int third = second + 1; third < n; ++third) {
        const double lhs = pair_activity[static_cast<std::size_t>(first) * n + second] +
                           pair_activity[static_cast<std::size_t>(first) * n + third] +
                           pair_activity[static_cast<std::size_t>(second) * n + third] -
                           2.0 * triple_activity[triple_key(first, second, third)];
        if (lhs > 1.0 + tolerance) {
          Sr3Cut cut(first, second, third);
          violations.push_back({cut, lhs, lhs - 1.0});
        }
      }
    }
  }
  std::sort(violations.begin(), violations.end(),
            [](const Sr3Violation& left, const Sr3Violation& right) {
              return left.violation > right.violation;
            });
  if (violations.size() > max_cuts) violations.erase(violations.begin() + static_cast<std::ptrdiff_t>(max_cuts), violations.end());
  return violations;
}

}  // namespace bpp
