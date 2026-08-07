#include "bpp/branching.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bpp {

RyanFosterConstraint::RyanFosterConstraint(int first_item, int second_item,
                                           PairRelation pair_relation)
    : first(std::min(first_item, second_item)),
      second(std::max(first_item, second_item)),
      relation(pair_relation) {
  if (first_item < 0 || second_item < 0 || first_item == second_item) {
    throw std::invalid_argument("Ryan-Foster branching requires two distinct non-negative items");
  }
}

bool RyanFosterConstraint::accepts(const Pattern& pattern) const {
  const auto& items = pattern.items();
  const bool has_first = std::binary_search(items.begin(), items.end(), first);
  const bool has_second = std::binary_search(items.begin(), items.end(), second);
  if (relation == PairRelation::Together) return has_first == has_second;
  return !(has_first && has_second);
}

BranchingState BranchingState::child(RyanFosterConstraint constraint) const {
  auto child_constraints = constraints_;
  child_constraints.push_back(std::move(constraint));
  return BranchingState(std::move(child_constraints));
}

bool BranchingState::accepts(const Pattern& pattern) const {
  return std::all_of(constraints_.begin(), constraints_.end(),
                     [&pattern](const RyanFosterConstraint& constraint) {
                       return constraint.accepts(pattern);
                     });
}

FractionalPair select_fractional_pair(const Instance& instance,
                                      const std::vector<Pattern>& patterns,
                                      const std::vector<double>& column_values,
                                      double tolerance) {
  if (patterns.size() != column_values.size()) {
    throw std::invalid_argument("one master value is required for every pattern");
  }
  if (tolerance < 0.0 || !std::isfinite(tolerance)) {
    throw std::invalid_argument("pair branching tolerance must be finite and non-negative");
  }

  FractionalPair selected;
  const std::size_t n = instance.item_count();
  for (std::size_t first = 0; first < n; ++first) {
    for (std::size_t second = first + 1; second < n; ++second) {
      double together = 0.0;
      for (std::size_t column = 0; column < patterns.size(); ++column) {
        const auto& items = patterns[column].items();
        const bool has_first = std::binary_search(items.begin(), items.end(), static_cast<int>(first));
        const bool has_second = std::binary_search(items.begin(), items.end(), static_cast<int>(second));
        if (has_first && has_second) together += column_values[column];
      }
      const double fractionality = std::min(together - std::floor(together),
                                            std::ceil(together) - together);
      if (fractionality <= tolerance) continue;
      // Legacy tie-break (BPPS_BP_TREE.cpp:314): among pairs equally close to
      // 0.5, prefer the pair with the larger combined item weight.
      const bool strictly_better = fractionality > selected.fractionality + tolerance;
      const bool tied = selected.first >= 0 &&
                        std::abs(fractionality - selected.fractionality) <= tolerance;
      const bool wins_tiebreak =
          tied && instance.weights()[static_cast<std::size_t>(first)] +
                          instance.weights()[static_cast<std::size_t>(second)] >
                      instance.weights()[static_cast<std::size_t>(selected.first)] +
                          instance.weights()[static_cast<std::size_t>(selected.second)];
      if (strictly_better || wins_tiebreak) {
        selected = {static_cast<int>(first), static_cast<int>(second), together, fractionality};
      }
    }
  }
  return selected;
}

FractionalPair select_most_confirmed_pair(const Instance& instance,
                                          const std::vector<Pattern>& patterns,
                                          const std::vector<double>& column_values,
                                          double tolerance) {
  if (patterns.size() != column_values.size()) {
    throw std::invalid_argument("one master value is required for every pattern");
  }
  if (tolerance < 0.0 || !std::isfinite(tolerance)) {
    throw std::invalid_argument("pair branching tolerance must be finite and non-negative");
  }

  FractionalPair selected;
  const std::size_t n = instance.item_count();
  for (std::size_t first = 0; first < n; ++first) {
    for (std::size_t second = first + 1; second < n; ++second) {
      double together = 0.0;
      for (std::size_t column = 0; column < patterns.size(); ++column) {
        const auto& items = patterns[column].items();
        const bool has_first = std::binary_search(items.begin(), items.end(), static_cast<int>(first));
        const bool has_second = std::binary_search(items.begin(), items.end(), static_cast<int>(second));
        if (has_first && has_second) together += column_values[column];
      }
      // Fractional (not already integral) and, among those, closest to full
      // integrality (largest value below 1) rather than closest to 0.5.
      const double fractionality = std::min(together - std::floor(together),
                                            std::ceil(together) - together);
      if (fractionality <= tolerance || together >= 1.0 - tolerance) continue;
      if (together > selected.together_value + tolerance) {
        selected = {static_cast<int>(first), static_cast<int>(second), together, fractionality};
      }
    }
  }
  return selected;
}

}  // namespace bpp
