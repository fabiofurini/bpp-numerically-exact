#pragma once

#include "bpp/instance.hpp"
#include "bpp/pattern.hpp"
#include "bpp/branching.hpp"
#include "bpp/cuts.hpp"

#include <optional>
#include <cstdint>
#include <vector>

namespace bpp {

struct PricingResult {
  std::optional<Pattern> pattern;
  double dual_value = 0.0;
  double reduced_cost = 0.0;
};

// Solves the classical BPP root-node pricing problem
// max sum_i dual[i] x_i, subject to sum_i weight[i] x_i <= capacity.
// A negative reduced cost means that the returned pattern must be added to the
// restricted master. Branching restrictions and SR3 dual terms are added by
// the label-setting pricer layered on top of this root oracle.
class FloatingRootPricer {
 public:
  explicit FloatingRootPricer(double bin_cost = 1.0) : bin_cost_(bin_cost) {}

  PricingResult price(const Instance& instance, const std::vector<double>& duals) const;
  std::vector<PricingResult> price_candidates(const Instance& instance,
                                              const std::vector<double>& duals,
                                              std::size_t max_candidates = 64) const;
  std::vector<PricingResult> price_label_setting(const Instance& instance,
                                                 const std::vector<double>& duals,
                                                 std::size_t max_candidates = 256) const;
  PricingResult price_with_sr3(const Instance& instance,
                               const std::vector<double>& duals,
                               const std::vector<Sr3Cut>& cuts) const;
  // Same contract as price_label_setting (capacity-indexed label-setting DP,
  // batched candidates), extended with one extra per-label state per active
  // SR3 cut: how many of its three items (clamped to 2) are already in the
  // partial pattern. The dual bonus is added exactly once, the step a cut's
  // count crosses from 1 to 2, matching Sr3Cut::coefficient. This replaces
  // the exponential-ish price_with_sr3 DFS as the batched root oracle once
  // cuts are active; see docs/STATUS.md for the measured gap it closes.
  std::vector<PricingResult> price_label_setting_with_sr3(
      const Instance& instance, const std::vector<double>& duals,
      const std::vector<Sr3Cut>& cuts, std::size_t max_candidates = 256) const;
  PricingResult price_with_branching_and_sr3(
      const Instance& instance, const std::vector<double>& duals,
      const BranchingState& branching, const std::vector<Sr3Cut>& cuts) const;

  // Exact branch-aware pricing oracle. Together constraints are contracted
  // into super-items and Different constraints are enforced as conflicts
  // during a capacity-bounded branch-and-bound search. The root overload
  // above remains the fast pseudo-polynomial knapsack implementation. Kept
  // as the correctness reference for price_label_setting_with_branching_and_sr3
  // below (and as its fallback for the pathological case of more
  // simultaneous conflicts/cuts than that DP's packed state can hold); no
  // longer used on the batch pricing loop's hot path.
  PricingResult price(const Instance& instance, const std::vector<double>& duals,
                      const BranchingState& branching) const;

  // Batched branch-aware pricing: the sparse label-setting DP
  // (price_label_setting/price_label_setting_with_sr3) run over
  // Together-contracted elements with Different conflicts enforced by a
  // packed per-label bitmask, instead of the one-column-per-call DFS above.
  // This is what closes the branching-node pricing gap documented in
  // docs/STATUS.md/REPORT_REFACTORING_BPP.tex; see the function definition
  // for the legacy-source verification this is based on. `cuts` may be
  // empty (branching-only) or non-empty (branching + SR3); `branching` may
  // also be empty, in which case this simply delegates to the two functions
  // above.
  std::vector<PricingResult> price_label_setting_with_branching_and_sr3(
      const Instance& instance, const std::vector<double>& duals,
      const BranchingState& branching, const std::vector<Sr3Cut>& cuts,
      std::size_t max_candidates = 256) const;

 private:
  double bin_cost_;
};

// Algorithm 1's exact pricing step (Baldacci et al. 2023, Sec. 3.1/3.3):
// prices under the *scaled-integer* duals (p_int, r_int) = floor(K *
// (p_float, r_float)) rather than raw floating duals, so the returned
// reduced cost is the exact scaled-integer reduced cost c_S(p_int, r_int,
// K) -- an exact integer (represented as a double, always safely
// round-trippable via llround) -- not a floating-point approximation.
// Implemented by reusing FloatingRootPricer::price_label_setting_with_sr3
// verbatim with bin_cost_ = K: every int64 scaled dual converts to an exact
// double (the caller must keep per-pattern sums under 2^53, enforced here),
// and the DP only adds/compares these values, so no precision is lost.
// `cuts` must carry already-scaled-integer dual values (cut.dual set to
// the scaled cut dual, as an exact double, e.g. via
// static_cast<double>(scaled_cut_dual)).
std::vector<PricingResult> price_scaled_integer_with_sr3(
    const Instance& instance, const std::vector<std::int64_t>& scaled_duals,
    std::int64_t scale, const std::vector<Sr3Cut>& cuts,
    std::size_t max_candidates = 256);

// Branch-aware counterpart of price_scaled_integer_with_sr3, for non-root
// branch-and-price nodes (Ryan-Foster Together/Different constraints
// active). Same exactness contract, reusing
// FloatingRootPricer::price_label_setting_with_branching_and_sr3.
std::vector<PricingResult> price_scaled_integer_with_branching_and_sr3(
    const Instance& instance, const std::vector<std::int64_t>& scaled_duals,
    std::int64_t scale, const BranchingState& branching, const std::vector<Sr3Cut>& cuts,
    std::size_t max_candidates = 256);

struct FixedPointPricingResult {
  std::optional<Pattern> pattern;
  std::int64_t dual_value = 0;
  std::int64_t reduced_cost = 0;
};

// Integer counterpart used by the safe pricing phase. The caller supplies
// already-scaled integer duals and bin cost; no floating-point comparison is
// made while selecting or rejecting a column.
class FixedPointRootPricer {
 public:
  explicit FixedPointRootPricer(std::int64_t bin_cost = 1) : bin_cost_(bin_cost) {}

  FixedPointPricingResult price(const Instance& instance,
                                const std::vector<std::int64_t>& duals) const;

 private:
  std::int64_t bin_cost_;
};

}  // namespace bpp
