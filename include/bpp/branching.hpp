#pragma once

#include "bpp/pattern.hpp"

#include <cstddef>
#include <vector>

namespace bpp {

enum class PairRelation { Together, Different };

struct RyanFosterConstraint {
  int first = -1;
  int second = -1;
  PairRelation relation = PairRelation::Together;

  RyanFosterConstraint(int first_item, int second_item, PairRelation pair_relation);
  bool accepts(const Pattern& pattern) const;
};

// Constraints accumulated on one branch-and-price node. The state is
// immutable from the caller's perspective and can be copied for child nodes.
class BranchingState {
 public:
  BranchingState() = default;
  explicit BranchingState(std::vector<RyanFosterConstraint> constraints)
      : constraints_(std::move(constraints)) {}

  BranchingState child(RyanFosterConstraint constraint) const;
  bool accepts(const Pattern& pattern) const;
  const std::vector<RyanFosterConstraint>& constraints() const noexcept { return constraints_; }

 private:
  std::vector<RyanFosterConstraint> constraints_;
};

// Selects the most fractional pair from a restricted-master solution. The
// returned pair is empty when no pair has a fractional co-occurrence.
struct FractionalPair {
  int first = -1;
  int second = -1;
  double together_value = 0.0;
  double fractionality = 0.0;
};

FractionalPair select_fractional_pair(const Instance& instance,
                                      const std::vector<Pattern>& patterns,
                                      const std::vector<double>& column_values,
                                      double tolerance = 1e-9);

// Selects the fractional pair whose co-occurrence value is closest to 1
// (largest value below full integrality) rather than closest to 0.5. This
// is the historical diving variable-selection rule ("IJOC",
// BPPS_BP_DIVING.cpp, PARAM_VAR_SELECTION_DIV=1): diving is meant to
// *confirm* what the LP relaxation is already leaning towards, not to
// resolve the most uncertain pair the way tree branching does, so it picks
// the pair the master already wants together most.
FractionalPair select_most_confirmed_pair(const Instance& instance,
                                          const std::vector<Pattern>& patterns,
                                          const std::vector<double>& column_values,
                                          double tolerance = 1e-9);

}  // namespace bpp
