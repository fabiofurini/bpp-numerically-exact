#include "bpp/pricing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <limits>
#include <stdexcept>
#include <utility>
#include <functional>
#include <numeric>
#include <set>
#include <unordered_map>
#include <cstdint>

namespace bpp {

PricingResult FloatingRootPricer::price(const Instance& instance, const std::vector<double>& duals) const {
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }

  const int capacity = instance.capacity();
  std::vector<double> best(static_cast<std::size_t>(capacity + 1), 0.0);
  std::vector<std::vector<bool>> take(
      instance.item_count(), std::vector<bool>(static_cast<std::size_t>(capacity + 1), false));

  std::vector<int> order(instance.item_count());
  std::iota(order.begin(), order.end(), 0);
  order.erase(std::remove_if(order.begin(), order.end(), [&duals](int item) {
                return duals[static_cast<std::size_t>(item)] <= 0.0;
              }), order.end());
  std::sort(order.begin(), order.end(), [&instance, &duals](int left, int right) {
    const double left_ratio = duals[static_cast<std::size_t>(left)] /
                              instance.weights()[static_cast<std::size_t>(left)];
    const double right_ratio = duals[static_cast<std::size_t>(right)] /
                               instance.weights()[static_cast<std::size_t>(right)];
    if (left_ratio != right_ratio) return left_ratio > right_ratio;
    return left < right;
  });

  for (const int item_index : order) {
    const auto item = static_cast<std::size_t>(item_index);
    const int weight = instance.weights()[item];
    for (int remaining = capacity; remaining >= weight; --remaining) {
      const double candidate = best[static_cast<std::size_t>(remaining - weight)] + duals[item];
      if (candidate > best[static_cast<std::size_t>(remaining)]) {
        best[static_cast<std::size_t>(remaining)] = candidate;
        take[item][static_cast<std::size_t>(remaining)] = true;
      }
    }
  }

  int best_capacity = 0;
  for (int used_capacity = 1; used_capacity <= capacity; ++used_capacity) {
    if (best[static_cast<std::size_t>(used_capacity)] > best[static_cast<std::size_t>(best_capacity)]) {
      best_capacity = used_capacity;
    }
  }

  std::vector<int> items;
  int remaining = best_capacity;
  for (std::size_t position = order.size(); position-- > 0;) {
    const auto item = static_cast<std::size_t>(order[position]);
    if (take[item][static_cast<std::size_t>(remaining)]) {
      items.push_back(static_cast<int>(item));
      remaining -= instance.weights()[item];
    }
  }

  PricingResult result;
  result.dual_value = best[static_cast<std::size_t>(best_capacity)];
  result.reduced_cost = bin_cost_ - result.dual_value;
  if (!items.empty()) result.pattern.emplace(instance, std::move(items));
  return result;
}

std::vector<PricingResult> FloatingRootPricer::price_candidates(
    const Instance& instance, const std::vector<double>& duals,
    std::size_t max_candidates) const {
  if (max_candidates == 0) return {};
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }
  const int capacity = instance.capacity();
  std::vector<double> best(static_cast<std::size_t>(capacity + 1), 0.0);
  std::vector<std::vector<bool>> take(
      instance.item_count(), std::vector<bool>(static_cast<std::size_t>(capacity + 1), false));
  std::vector<int> order(instance.item_count());
  std::iota(order.begin(), order.end(), 0);
  order.erase(std::remove_if(order.begin(), order.end(), [&duals](int item) {
                return duals[static_cast<std::size_t>(item)] <= 0.0;
              }), order.end());
  std::sort(order.begin(), order.end(), [&instance, &duals](int left, int right) {
    const double lr = duals[static_cast<std::size_t>(left)] / instance.weights()[static_cast<std::size_t>(left)];
    const double rr = duals[static_cast<std::size_t>(right)] / instance.weights()[static_cast<std::size_t>(right)];
    if (lr != rr) return lr > rr;
    return left < right;
  });
  for (int item_index : order) {
    const auto item = static_cast<std::size_t>(item_index);
    const int weight = instance.weights()[item];
    for (int remaining = capacity; remaining >= weight; --remaining) {
      const double candidate = best[static_cast<std::size_t>(remaining - weight)] + duals[item];
      if (candidate > best[static_cast<std::size_t>(remaining)]) {
        best[static_cast<std::size_t>(remaining)] = candidate;
        take[item][static_cast<std::size_t>(remaining)] = true;
      }
    }
  }
  std::vector<PricingResult> candidates;
  std::set<std::vector<int>> seen;
  for (int used_capacity = 1; used_capacity <= capacity; ++used_capacity) {
    const double value = best[static_cast<std::size_t>(used_capacity)];
    if (value <= bin_cost_) continue;
    std::vector<int> items;
    int remaining = used_capacity;
    for (std::size_t position = order.size(); position-- > 0;) {
      const auto item = static_cast<std::size_t>(order[position]);
      if (take[item][static_cast<std::size_t>(remaining)]) {
        items.push_back(static_cast<int>(item));
        remaining -= instance.weights()[item];
      }
    }
    if (items.empty() || !seen.insert(items).second) continue;
    PricingResult result;
    result.dual_value = value;
    result.reduced_cost = bin_cost_ - value;
    result.pattern.emplace(instance, std::move(items));
    candidates.push_back(std::move(result));
  }
  std::sort(candidates.begin(), candidates.end(), [](const PricingResult& left, const PricingResult& right) {
    return left.reduced_cost < right.reduced_cost;
  });
  if (candidates.size() > max_candidates) candidates.resize(max_candidates);
  return candidates;
}

namespace {

// One label of a 1D knapsack label-setting frontier: `load` capacity used,
// `value` accumulated dual profit, `path` indexes into a PathNode arena for
// reconstruction (-1 is the empty pattern).
struct Label {
  int load;
  double value;
  int path;
};

// Merges two frontiers that are each already sorted ascending by load, and
// prunes dominated labels in the same pass: label A dominates B when
// A.load <= B.load and A.value >= B.value, since any extension still
// available to B is also available to A, so B can never lead to a strictly
// better final pattern. Keeping the frontier sparse (only genuinely
// non-dominated (load, value) pairs) instead of a dense array indexed by
// every integer load from 0 to capacity is what lets this DP stay fast on
// large-capacity instances: the legacy label-setting pricer (DP.cpp,
// mckpsc-ls) does the same and empirically keeps on the order of a hundred
// labels per call even at capacity in the thousands (see docs/STATUS.md,
// ANI-402 finding) — capacity itself is not the cost driver once dominance
// is applied, only the number of genuinely distinct trade-offs is.
// Writes the merged, dominance-pruned frontier into `out` (cleared first).
// `scratch` is caller-owned reusable storage for the merge step, so that a
// per-item loop calling this every iteration only pays for vector growth
// once, not a fresh heap allocation every time -- with a DP call routinely
// running this a few hundred times (once per item), that allocation churn
// was itself a measurable fraction of the total pricing time (see
// docs/STATUS.md, ANI-402 checkpoint).
void merge_prune_frontier(const std::vector<Label>& kept, const std::vector<Label>& extended,
                          std::vector<Label>& scratch, std::vector<Label>& out) {
  scratch.clear();
  scratch.reserve(kept.size() + extended.size());
  std::merge(kept.begin(), kept.end(), extended.begin(), extended.end(), std::back_inserter(scratch),
            [](const Label& left, const Label& right) { return left.load < right.load; });
  out.clear();
  out.reserve(scratch.size());
  double best_value = -std::numeric_limits<double>::infinity();
  for (const auto& label : scratch) {
    if (label.value > best_value) {
      out.push_back(label);
      best_value = label.value;
    }
  }
}

// Precomputed prefix sums (in ratio order) of item weight and dual value,
// answering "best possible additional value from order[position..), packed
// greedily into `remaining` units of capacity, with one item allowed to
// split fractionally" in O(log n) -- the classic fractional-knapsack LP
// relaxation bound. This is deliberately tighter than a flat "sum of every
// remaining item's dual, ignoring how much capacity is actually left"
// bound: the flat bound barely prunes a label-setting frontier on a large,
// high-capacity instance, which is what made ANI-402 (402 items, capacity
// ~7550) blow up to thousands of surviving labels per pricing call before
// this bound was tightened (see docs/STATUS.md).
class FractionalBoundTable {
 public:
  FractionalBoundTable(const Instance& instance, const std::vector<double>& duals,
                       const std::vector<int>& order)
      : cum_weight_(order.size() + 1, 0.0), cum_value_(order.size() + 1, 0.0) {
    for (std::size_t position = 0; position < order.size(); ++position) {
      const auto item = static_cast<std::size_t>(order[position]);
      cum_weight_[position + 1] = cum_weight_[position] + instance.weights()[item];
      cum_value_[position + 1] = cum_value_[position] + duals[item];
    }
  }

  // Same bound, built directly from per-position weight/value arrays instead
  // of reading them off an Instance -- used by the branch-aware DP, whose
  // selectable units are Together-contracted super-items, not raw instance
  // items, so there is no single per-item dual to index by item id.
  FractionalBoundTable(const std::vector<int>& weight_by_position,
                       const std::vector<double>& value_by_position)
      : cum_weight_(weight_by_position.size() + 1, 0.0),
        cum_value_(weight_by_position.size() + 1, 0.0) {
    for (std::size_t position = 0; position < weight_by_position.size(); ++position) {
      cum_weight_[position + 1] = cum_weight_[position] + weight_by_position[position];
      cum_value_[position + 1] = cum_value_[position] + value_by_position[position];
    }
  }

  double bound(std::size_t position, int remaining) const {
    if (remaining <= 0) return 0.0;
    const double target = cum_weight_[position] + remaining;
    const auto it = std::upper_bound(cum_weight_.begin() + static_cast<std::ptrdiff_t>(position),
                                     cum_weight_.end(), target);
    const std::size_t end = static_cast<std::size_t>(it - cum_weight_.begin());
    const std::size_t whole_end = end - 1;  // end > position always, since remaining > 0
    double result = cum_value_[whole_end] - cum_value_[position];
    if (whole_end + 1 < cum_weight_.size()) {
      const double item_weight = cum_weight_[whole_end + 1] - cum_weight_[whole_end];
      const double item_value = cum_value_[whole_end + 1] - cum_value_[whole_end];
      const double left = remaining - (cum_weight_[whole_end] - cum_weight_[position]);
      if (item_weight > 0.0) result += item_value * std::min(1.0, left / item_weight);
    }
    return result;
  }

 private:
  std::vector<double> cum_weight_;
  std::vector<double> cum_value_;
};

// Value-based SR3 dominance, replacing the earlier state-covering rule
// (see git history / docs/STATUS.md for the retired cut_state_covers) with
// a port of legacy's actual mechanism (mckpsc_ls_alg_dominance,
// mckpsc-ls.cpp:2060-2208): label A (value_a, 1-bit-per-cut parity state
// state_a) dominates label B (value_b, state_b), given load_a <= load_b,
// iff A's value is still >= B's after charging A the worst-case extra SR3
// risk it carries relative to B:
//
//   value_a >= value_b + margin,  margin = sum over cuts s of:
//     dual_s < 0 (penalty cut) and A primed, B not primed:  -dual_s
//     dual_s > 0 (bonus  cut) and B primed, A not primed:   +dual_s
//
// "Primed" means the parity bit is set: this label has seen an odd number
// (1 or 3) of this cut's three items so far, so its *next* encountered
// member item would toggle the bit and apply the cut's dual to the running
// value (see the increment logic below) -- i.e. the bit tracks "is this
// label one step from a value change on this cut", not the exact 0/1/2/3
// count, which is legacy's actual representation (1 bit per cut, not 2)
// and is what lets it track hundreds of simultaneous cuts in one bitmask
// instead of the ~20-cut ceiling a 2-bit-per-cut packed count imposes.
//
// Derivation for classical BPP (no Ryan-Foster conflicts): legacy's full
// dominance rule (used for the general MC-KP-SC engine) also has an
// "rcLeftItems" term crediting B for remaining reachable items A cannot
// reach. For plain capacity (no conflicts), load_a <= load_b means A's
// remaining capacity is >= B's, so every item B can still fit, A can too
// -- that term is provably empty and drops out, leaving exactly the
// formula above. The margin's sign-generalization to positive-dual cuts
// (legacy only ever sees dual <= 0, since an SR3 "<=1" row's dual in a
// minimization primal is never positive by LP duality) mirrors this
// codebase's own established sign-aware convention from the retired
// state-covering rule, so this stays sound even if a positive dual is
// ever observed numerically.
//
// Soundness note: a cut whose 3 members have *all* already been placed or
// passed by both labels at this position can no longer change value for
// either, so it contributes no real future risk -- but the formula still
// charges a margin for it if the (now-frozen) bits differ. That makes the
// rule strictly *more* conservative than necessary in that edge case
// (fewer prunes, never an incorrect one), not unsound.
double sr3_dominance_margin(std::uint64_t state_a, std::uint64_t state_b,
                            const std::vector<Sr3Cut>& cuts) {
  double margin = 0.0;
  for (std::size_t cut = 0; cut < cuts.size(); ++cut) {
    const std::uint64_t bit = 1ULL << cut;
    const bool a_primed = (state_a & bit) != 0;
    const bool b_primed = (state_b & bit) != 0;
    const double dual = cuts[cut].dual;
    if (dual < 0.0) {
      if (a_primed && !b_primed) margin += -dual;
    } else if (dual > 0.0) {
      if (b_primed && !a_primed) margin += dual;
    }
  }
  return margin;
}

// Removes SR3 labels dominated by another label in the same map, using
// sr3_dominance_margin above instead of the retired state-covering rule.
// Two cost controls, both needed (see docs/STATUS.md, two prior attempts
// with only a size threshold on a full O(n^2) sweep both measured
// net-negative): (1) only runs once the map exceeds `threshold` labels, so
// the sweep never pays overhead on the common small-map case; (2) entries
// are sorted by load ascending and each is only compared against the next
// `window` entries in that order (not the full remaining O(n) suffix),
// bounding total cost to O(n*window) instead of O(n^2) -- mirrors the
// legacy DP's own bounded rc-sorted comparison window (mckpsc-ls.cpp,
// PARAM_DELTA), which is incomplete/heuristic by the same design: it can
// miss some dominated labels (sound, just not exhaustive), never prunes a
// label that isn't actually dominated. Only checks "entries[i] dominates
// entries[j]" (i.e. earlier-in-load-order dominates later), not the
// reverse -- same asymmetric-but-sound limitation the retired rule had.
template <typename LabelInfo>
void prune_dominated_sr3_labels(std::unordered_map<std::uint64_t, LabelInfo>& labels,
                                const std::vector<Sr3Cut>& cuts, std::size_t threshold,
                                std::size_t window) {
  if (labels.size() <= threshold) return;
  std::vector<std::pair<std::uint64_t, LabelInfo>> entries(labels.begin(), labels.end());
  std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
    return (left.first >> 40) < (right.first >> 40);  // ascending load
  });
  std::vector<bool> dominated(entries.size(), false);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (dominated[i]) continue;
    const std::uint64_t state_i = entries[i].first & ((1ULL << 40) - 1);
    const std::size_t window_end = std::min(i + 1 + window, entries.size());
    for (std::size_t j = i + 1; j < window_end; ++j) {
      if (dominated[j]) continue;
      // entries is sorted by load ascending, so load_i <= load_j here --
      // the precondition sr3_dominance_margin's derivation relies on.
      const std::uint64_t state_j = entries[j].first & ((1ULL << 40) - 1);
      const double margin = sr3_dominance_margin(state_i, state_j, cuts);
      if (entries[i].second.value >= entries[j].second.value + margin) {
        dominated[j] = true;
      }
    }
  }
  std::unordered_map<std::uint64_t, LabelInfo> pruned;
  pruned.reserve(entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (!dominated[i]) pruned.emplace(entries[i].first, entries[i].second);
  }
  labels = std::move(pruned);
}

// One Together-contracted selectable unit for branch-aware pricing: a set of
// original items that must be selected as a block (weight/value already
// summed), mirroring legacy's super-item construction exactly
// (BPPS_BP_MAPPING.cpp, absorbe(): items forced Together via Ryan-Foster
// branching are merged into one indivisible pseudo-item with summed weight
// and summed dual, DP.cpp:112-163). A `forbidden` element is one that a
// Different constraint has, through a chain of Together merges, forced into
// conflict with itself -- an infeasible branch outcome that must be excluded
// rather than priced (mirrors conflicts_super_item's self-conflict handling,
// BPPS_BP_MAPPING.cpp:234-239).
struct BranchElement {
  std::vector<int> items;
  int weight = 0;
  double value = 0.0;
  bool forbidden = false;
};

struct BranchGroups {
  std::vector<BranchElement> elements;
  // conflicts[e] lists every other element index e may not coexist with in
  // the same pattern (a Different constraint between the two, possibly after
  // Together contraction folded the original items into different elements).
  std::vector<std::vector<int>> conflicts;
};

// Builds the branch-consistent selectable-element universe for one pricing
// call. This is the same item-set transformation legacy performs once per
// branch-and-price node before every pricing call at that node
// (BPPS_BP_MAPPING.cpp's MAPPING_UPDATE, consumed by DP.cpp's
// prepare_data_DP_LABEL_SETTING) rather than carrying branching state inside
// the DP label itself -- Different conflicts are recorded here as an
// adjacency list and enforced during label extension (see
// price_label_setting_with_branching_and_sr3), exactly mirroring how
// mckpsc_ls_alg_setlabel prunes a label's remaining-eligible item set the
// moment a conflicting item is fixed in (mckpsc-ls.cpp:2539-2551), not by
// adding a new per-label dominance dimension.
BranchGroups build_branch_groups(const Instance& instance, const std::vector<double>& duals,
                                 const BranchingState& branching) {
  const std::size_t n = instance.item_count();
  std::vector<int> parent(n);
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> find = [&](int item) {
    while (parent[static_cast<std::size_t>(item)] != item) {
      parent[static_cast<std::size_t>(item)] =
          parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(item)])];
      item = parent[static_cast<std::size_t>(item)];
    }
    return item;
  };
  for (const auto& constraint : branching.constraints()) {
    if (constraint.relation == PairRelation::Together) {
      const int left = find(constraint.first);
      const int right = find(constraint.second);
      if (left != right) parent[static_cast<std::size_t>(right)] = left;
    }
  }

  std::vector<int> roots;
  std::vector<int> element_of_root(n, -1);
  for (int item = 0; item < static_cast<int>(n); ++item) {
    const int root = find(item);
    if (element_of_root[static_cast<std::size_t>(root)] < 0) {
      element_of_root[static_cast<std::size_t>(root)] = static_cast<int>(roots.size());
      roots.push_back(root);
    }
  }

  BranchGroups result;
  result.elements.resize(roots.size());
  for (int item = 0; item < static_cast<int>(n); ++item) {
    auto& element = result.elements[static_cast<std::size_t>(element_of_root[static_cast<std::size_t>(find(item))])];
    element.items.push_back(item);
    element.weight += instance.weights()[static_cast<std::size_t>(item)];
    element.value += duals[static_cast<std::size_t>(item)];
  }

  result.conflicts.assign(result.elements.size(), {});
  for (const auto& constraint : branching.constraints()) {
    if (constraint.relation != PairRelation::Different) continue;
    const int left = element_of_root[static_cast<std::size_t>(find(constraint.first))];
    const int right = element_of_root[static_cast<std::size_t>(find(constraint.second))];
    if (left == right) {
      result.elements[static_cast<std::size_t>(left)].forbidden = true;
    } else {
      result.conflicts[static_cast<std::size_t>(left)].push_back(right);
      result.conflicts[static_cast<std::size_t>(right)].push_back(left);
    }
  }
  return result;
}

// Label key for the branch-aware SR3 DP: capacity used, packed SR3 cut-state
// (same 1-bit-per-cut parity scheme as price_label_setting_with_sr3, see
// sr3_dominance_margin's doc comment), and a packed conflict-slot bitmask (1
// bit per Together-element that participates in at least one Different
// constraint, set once that element has been selected on this path). Kept as
// three separate fields rather than packed into one 64-bit int like the
// cut-only DP: branching nodes need both cut and conflict state
// simultaneously, and jamming both into 64 bits alongside load would leave
// too few bits to be a safe general-purpose cap.
struct BranchKey {
  std::uint32_t load;
  std::uint64_t cut_state;
  std::uint64_t conflict_mask;
};
struct BranchKeyEq {
  bool operator()(const BranchKey& a, const BranchKey& b) const noexcept {
    return a.load == b.load && a.cut_state == b.cut_state && a.conflict_mask == b.conflict_mask;
  }
};
struct BranchKeyHash {
  std::size_t operator()(const BranchKey& k) const noexcept {
    std::size_t h = std::hash<std::uint32_t>()(k.load);
    h ^= std::hash<std::uint64_t>()(k.cut_state) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= std::hash<std::uint64_t>()(k.conflict_mask) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h;
  }
};

}  // namespace

std::vector<PricingResult> FloatingRootPricer::price_label_setting(
    const Instance& instance, const std::vector<double>& duals,
    std::size_t max_candidates) const {
  if (max_candidates == 0) return {};
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }

  const int capacity = instance.capacity();
  struct PathNode { int item; int previous; };
  std::vector<PathNode> nodes;
  std::vector<int> order(instance.item_count());
  std::iota(order.begin(), order.end(), 0);
  order.erase(std::remove_if(order.begin(), order.end(), [&duals](int item) {
                return duals[static_cast<std::size_t>(item)] <= 0.0;
              }), order.end());
  std::sort(order.begin(), order.end(), [&instance, &duals](int left, int right) {
    const double lr = duals[static_cast<std::size_t>(left)] /
                      instance.weights()[static_cast<std::size_t>(left)];
    const double rr = duals[static_cast<std::size_t>(right)] /
                      instance.weights()[static_cast<std::size_t>(right)];
    if (lr != rr) return lr > rr;
    return left < right;
  });

  const FractionalBoundTable bound_table(instance, duals, order);
  // Safety margin absorbing FractionalBoundTable's own floating-point
  // rounding (a genuine fractional split-item term, ~bin_cost_ * 2^-52
  // worst case) so pruning never discards a label whose *true* fractional
  // bound was exactly at the threshold -- needed so this DP can be reused
  // as the exact fixed-point pricer with bin_cost_ set to a large integer
  // scale K (see price_scaled_integer_with_sr3): 1e-9 relative is ~1e7x the
  // worst-case rounding at any K used in practice, while staying far below
  // reduced_cost_tolerance for the real (bin_cost_=1) case, so it does not
  // change existing floating-dual pricing behavior.
  const double bound_margin = bin_cost_ * 1e-9 + 1e-12;

  std::vector<PricingResult> candidates;
  std::set<std::vector<int>> seen;
  auto reconstruct = [&](int node_index) {
    std::vector<int> items;
    while (node_index >= 0) {
      items.push_back(nodes[static_cast<std::size_t>(node_index)].item);
      node_index = nodes[static_cast<std::size_t>(node_index)].previous;
    }
    std::sort(items.begin(), items.end());
    return items;
  };
  auto consider = [&](double value, int node_index) {
    const double reduced_cost = bin_cost_ - value;
    if (reduced_cost >= 0.0) return;
    if (candidates.size() >= max_candidates &&
        reduced_cost >= candidates.back().reduced_cost) return;
    auto items = reconstruct(node_index);
    if (items.empty() || !seen.insert(items).second) return;
    PricingResult candidate;
    candidate.dual_value = value;
    candidate.reduced_cost = reduced_cost;
    candidate.pattern.emplace(instance, std::move(items));
    candidates.push_back(std::move(candidate));
    std::sort(candidates.begin(), candidates.end(),
              [](const PricingResult& left, const PricingResult& right) {
                return left.reduced_cost < right.reduced_cost;
              });
    if (candidates.size() > max_candidates) candidates.pop_back();
  };

  // The frontier: sorted ascending by load, value strictly increasing with
  // load (see merge_prune_frontier). Starts with just the empty pattern.
  // `extended`/`merge_scratch`/`next_frontier` are declared once and reused
  // (cleared, not reallocated) every item -- see merge_prune_frontier's
  // comment for why that matters on large instances.
  std::vector<Label> frontier{{0, 0.0, -1}};
  std::vector<Label> extended;
  std::vector<Label> merge_scratch;
  std::vector<Label> next_frontier;
  for (std::size_t position = 0; position < order.size(); ++position) {
    const int item_index = order[position];
    const auto item = static_cast<std::size_t>(item_index);
    const int weight = instance.weights()[item];
    // Permanently drop labels that cannot reach bin_cost_ even packing the
    // rest of the capacity optimally (fractionally) with items[position..).
    // This bound only shrinks as position advances, so a label that fails
    // it now will fail it forever -- unlike the per-round `continue` below,
    // this removes it from the frontier for good, which is what keeps the
    // frontier from accumulating labels that can never lead anywhere (the
    // actual fix for the ANI-402 blow-up: a loose bound barely prunes).
    frontier.erase(std::remove_if(frontier.begin(), frontier.end(), [&](const Label& label) {
                     return label.value + bound_table.bound(position, capacity - label.load) + bound_margin <= bin_cost_;
                   }), frontier.end());
    if (frontier.empty()) break;

    // Extending `frontier` (item not taken) by this item (item taken);
    // iterating the frontier in load order keeps `extended` sorted too, so
    // the two runs can be merged in the usual sorted-merge sense below.
    extended.clear();
    for (const auto& label : frontier) {
      if (label.load + weight > capacity) continue;
      const double value = label.value + duals[item];
      if (value + bound_table.bound(position + 1, capacity - label.load - weight) + bound_margin <= bin_cost_) continue;
      nodes.push_back({item_index, label.path});
      extended.push_back({label.load + weight, value, static_cast<int>(nodes.size()) - 1});
    }
    const std::size_t new_node_floor = nodes.size() - extended.size();
    merge_prune_frontier(frontier, extended, merge_scratch, next_frontier);
    frontier.swap(next_frontier);
    // Only labels that both survived dominance pruning and were created this
    // iteration are new candidate patterns; re-running consider() on
    // unchanged old labels (whose reduced cost cannot have changed) would
    // just repeat the reconstruct()+seen.insert() cost of every earlier
    // iteration on every later one.
    for (const auto& label : frontier) {
      if (static_cast<std::size_t>(label.path) >= new_node_floor) consider(label.value, label.path);
    }
  }
  return candidates;
}

std::vector<PricingResult> FloatingRootPricer::price_label_setting_with_sr3(
    const Instance& instance, const std::vector<double>& duals,
    const std::vector<Sr3Cut>& cuts, std::size_t max_candidates) const {
  if (cuts.empty()) return price_label_setting(instance, duals, max_candidates);
  if (max_candidates == 0) return {};
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }

  const int capacity = instance.capacity();
  const auto k = cuts.size();
  // Per-label cut state is packed 1 bit per cut into a single integer key
  // instead of a heap-allocated vector per label: with thousands of labels
  // touched per pricing call, a vector-keyed map made this DP allocation-
  // bound and slower than the DFS it was meant to replace. Combining load
  // and packed cut state into one 64-bit key keeps every label transition
  // allocation-free. The bit is a PARITY flag, not a clamped count -- it
  // tracks whether an odd number (1 or 3) of the cut's three items have
  // been seen so far, i.e. whether the *next* one seen would trigger the
  // cut's dual (see the label-extension loop below and
  // sr3_dominance_margin's doc comment) -- this mirrors legacy's own
  // representation (mckpsc-ls.cpp's vbm_sr3, 1 bit per cut) rather than
  // this DP's earlier 2-bit clamped-count packing, and is what lets the
  // 40-bit reserved region here hold twice as many simultaneous cuts (40
  // instead of 20) for the same key width.
  if (k > 40) {
    throw std::invalid_argument("price_label_setting_with_sr3 supports at most 40 simultaneous cuts");
  }
  if (capacity < 0 || capacity >= (1 << 24)) {
    throw std::invalid_argument("price_label_setting_with_sr3 requires capacity below 2^24");
  }
  // Every cut a given item participates in; only those need a state entry
  // touched when the item is added to a label.
  std::vector<std::vector<std::size_t>> item_cuts(instance.item_count());
  for (std::size_t cut = 0; cut < k; ++cut) {
    item_cuts[static_cast<std::size_t>(cuts[cut].first)].push_back(cut);
    item_cuts[static_cast<std::size_t>(cuts[cut].second)].push_back(cut);
    item_cuts[static_cast<std::size_t>(cuts[cut].third)].push_back(cut);
  }

  struct PathNode { int item; int previous; };
  std::vector<PathNode> nodes;

  auto pack_key = [](int load, std::uint64_t cut_state) {
    return (static_cast<std::uint64_t>(load) << 40) | cut_state;
  };
  auto unpack_load = [](std::uint64_t key) { return static_cast<int>(key >> 40); };

  struct LabelInfo { double value; int path; };
  std::unordered_map<std::uint64_t, LabelInfo> labels;
  labels[pack_key(0, 0)] = {0.0, -1};

  std::vector<int> order(instance.item_count());
  std::iota(order.begin(), order.end(), 0);
  order.erase(std::remove_if(order.begin(), order.end(), [&duals](int item) {
                return duals[static_cast<std::size_t>(item)] <= 0.0;
              }), order.end());
  std::sort(order.begin(), order.end(), [&instance, &duals](int left, int right) {
    const double lr = duals[static_cast<std::size_t>(left)] /
                      instance.weights()[static_cast<std::size_t>(left)];
    const double rr = duals[static_cast<std::size_t>(right)] /
                      instance.weights()[static_cast<std::size_t>(right)];
    if (lr != rr) return lr > rr;
    return left < right;
  });

  // Same tight fractional-knapsack bound as price_label_setting (see
  // FractionalBoundTable), plus an optimistic flat allowance for whatever
  // of the still-unresolved cuts' duals could still be earned. The cut
  // allowance stays flat/capacity-unaware (a cut's dual does not depend on
  // capacity) but the item-value part respects remaining capacity, which is
  // what keeps this frontier from blowing up on large-capacity instances
  // the same way the cut-free DP did (see docs/STATUS.md, ANI-402 finding).
  const FractionalBoundTable bound_table(instance, duals, order);
  double positive_cut_bound = 0.0;
  for (const auto& cut : cuts) positive_cut_bound += std::max(0.0, cut.dual);
  // See price_label_setting's identical margin: absorbs the fractional
  // bound's own floating-point rounding so this DP stays sound when reused
  // with bin_cost_ set to a large integer scale K.
  const double bound_margin = bin_cost_ * 1e-9 + 1e-12;

  std::vector<PricingResult> candidates;
  std::set<std::vector<int>> seen;
  auto reconstruct = [&](int node_index) {
    std::vector<int> items;
    while (node_index >= 0) {
      items.push_back(nodes[static_cast<std::size_t>(node_index)].item);
      node_index = nodes[static_cast<std::size_t>(node_index)].previous;
    }
    std::sort(items.begin(), items.end());
    return items;
  };
  auto consider = [&](double value, int node_index) {
    const double reduced_cost = bin_cost_ - value;
    if (reduced_cost >= 0.0) return;
    if (candidates.size() >= max_candidates &&
        reduced_cost >= candidates.back().reduced_cost) return;
    auto items = reconstruct(node_index);
    if (items.empty() || !seen.insert(items).second) return;
    PricingResult candidate;
    candidate.dual_value = value;
    candidate.reduced_cost = reduced_cost;
    candidate.pattern.emplace(instance, std::move(items));
    candidates.push_back(std::move(candidate));
    std::sort(candidates.begin(), candidates.end(),
              [](const PricingResult& left, const PricingResult& right) {
                return left.reduced_cost < right.reduced_cost;
              });
    if (candidates.size() > max_candidates) candidates.pop_back();
  };

  for (std::size_t position = 0; position < order.size(); ++position) {
    const int item = order[position];
    const int weight = instance.weights()[static_cast<std::size_t>(item)];
    const auto& incident_cuts = item_cuts[static_cast<std::size_t>(item)];

    // Permanently drop labels that cannot reach bin_cost_ even packing the
    // rest of the capacity optimally with items[position..) plus every
    // still-available cut dual. Just like price_label_setting, this bound
    // only shrinks as position advances, so this is a one-way prune, not a
    // per-round skip -- the actual fix for the frontier blow-up.
    for (auto it = labels.begin(); it != labels.end();) {
      const int load = unpack_load(it->first);
      if (it->second.value + bound_table.bound(position, capacity - load) + positive_cut_bound + bound_margin <= bin_cost_) {
        it = labels.erase(it);
      } else {
        ++it;
      }
    }
    if (labels.empty()) break;
    // Value-based SR3 dominance (prune_dominated_sr3_labels, using
    // sr3_dominance_margin) is the fix for the ~3x-per-simultaneous-cut
    // frontier growth: the fathoming bound above only removes labels that
    // can never be improving, while this removes labels that are improving
    // but strictly dominated by another label already on the frontier.
    // Earlier attempts with a full O(n^2) sweep (2026-08-07 night, see
    // docs/STATUS.md) were measured net-negative -- the O(n^2) cost itself
    // was the problem. This call is bounded on both axes (size threshold
    // 500, matching legacy's dom_nlabels_thr, and a comparison window of 8
    // per entry instead of the full remaining suffix, mirroring legacy's
    // own bounded rc-sorted window, PARAM_DELTA), making it O(n*8) instead
    // of O(n^2). The dominance criterion itself was ported from legacy's
    // value-based (reduced-cost-margin) rule on 2026-08-08, replacing an
    // earlier state-covering rule that capped simultaneous cuts at 20 and
    // whose comparison cost grew with the cut-state space rather than
    // staying flat in the number of cuts -- see sr3_dominance_margin's doc
    // comment for the full derivation.
    prune_dominated_sr3_labels(labels, cuts, 500, 8);

    // A snapshot freezes the labels visible before this item, so each path
    // still selects every item at most once (mirrors the descending-capacity
    // trick price_label_setting uses for the same 0/1 reason).
    const std::unordered_map<std::uint64_t, LabelInfo> snapshot = labels;
    for (const auto& [key, info] : snapshot) {
      const int load = unpack_load(key);
      if (load + weight > capacity) continue;
      const double extended_value = info.value + duals[static_cast<std::size_t>(item)];
      if (extended_value + bound_table.bound(position + 1, capacity - load - weight) +
              positive_cut_bound + bound_margin <= bin_cost_) {
        continue;
      }
      // Toggle each incident cut's parity bit; a 1->0 transition (the bit
      // was already set, meaning this is the cut's 2nd or 4th... member
      // item seen) is the one that actually completes a pair and applies
      // the cut's dual, mirroring legacy's bit-toggle-with-penalty-
      // application timing (mckpsc-ls.cpp ~2460-2540) exactly, generalized
      // to either dual sign per sr3_dominance_margin's doc comment.
      const std::uint64_t cut_state = key & ((1ULL << 40) - 1);
      std::uint64_t new_state = cut_state;
      double bonus = 0.0;
      for (std::size_t cut : incident_cuts) {
        const std::uint64_t bit = 1ULL << cut;
        const bool was_primed = (new_state & bit) != 0;
        new_state ^= bit;
        if (was_primed) bonus += cuts[cut].dual;
      }
      const double new_value = extended_value + bonus;
      const auto new_key = pack_key(load + weight, new_state);
      auto existing = labels.find(new_key);
      if (existing != labels.end() && existing->second.value >= new_value) continue;
      nodes.push_back({item, info.path});
      const int node_index = static_cast<int>(nodes.size()) - 1;
      labels[new_key] = {new_value, node_index};
      consider(new_value, node_index);
    }
  }
  return candidates;
}

PricingResult FloatingRootPricer::price_with_sr3(const Instance& instance,
                                                 const std::vector<double>& duals,
                                                 const std::vector<Sr3Cut>& cuts) const {
  if (cuts.empty()) return price(instance, duals);
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }

  std::vector<int> order(instance.item_count());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&instance, &duals](int left, int right) {
    const double lr = duals[static_cast<std::size_t>(left)] /
                      instance.weights()[static_cast<std::size_t>(left)];
    const double rr = duals[static_cast<std::size_t>(right)] /
                      instance.weights()[static_cast<std::size_t>(right)];
    if (lr != rr) return lr > rr;
    return left < right;
  });
  const int capacity = instance.capacity();
  double positive_cut_bound = 0.0;
  for (const auto& cut : cuts) if (cut.dual > 0.0) positive_cut_bound += cut.dual;

  double best_value = 0.0;
  std::vector<int> selected;
  std::vector<int> best_selected;
  std::function<void(std::size_t, int, double)> search =
      [&](std::size_t position, int load, double item_value) {
        double cut_value = 0.0;
        if (!selected.empty()) {
          Pattern current(instance, selected);
          for (const auto& cut : cuts) cut_value += cut.dual * cut.coefficient(current);
        }
        const double value = item_value + cut_value;
        if (value > best_value) {
          best_value = value;
          best_selected = selected;
        }
        if (position == order.size()) return;
        int remaining = capacity - load;
        double bound = item_value + positive_cut_bound;
        for (std::size_t p = position; p < order.size() && remaining > 0; ++p) {
          const int item = order[p];
          const int weight = instance.weights()[static_cast<std::size_t>(item)];
          if (duals[static_cast<std::size_t>(item)] <= 0.0) continue;
          if (weight <= remaining) {
            remaining -= weight;
            bound += duals[static_cast<std::size_t>(item)];
          } else {
            bound += duals[static_cast<std::size_t>(item)] *
                     (static_cast<double>(remaining) / weight);
            break;
          }
        }
        if (bound <= best_value + 1e-12) return;
        const int item = order[position];
        const int weight = instance.weights()[static_cast<std::size_t>(item)];
        if (load + weight <= capacity) {
          selected.push_back(item);
          search(position + 1, load + weight,
                 item_value + duals[static_cast<std::size_t>(item)]);
          selected.pop_back();
        }
        search(position + 1, load, item_value);
      };
  search(0, 0, 0.0);

  PricingResult result;
  result.dual_value = best_value;
  result.reduced_cost = bin_cost_ - best_value;
  if (!best_selected.empty()) result.pattern.emplace(instance, std::move(best_selected));
  return result;
}

// Batched branch-aware pricing: the same sparse label-setting DP as
// price_label_setting_with_sr3, but run over Together-contracted elements
// (build_branch_groups) instead of raw items, with Different constraints
// enforced by a packed conflict-slot bitmask instead of the group-based DFS
// used by price(...,branching)/price_with_branching_and_sr3. This is the
// fix for the branching-node pricing bottleneck documented in
// docs/STATUS.md/REPORT_REFACTORING_BPP.tex ("il pricer dei nodi con
// branching e ancora la DFS lenta"): every non-root branch-and-price node
// was pricing one column per RMP resolve via a DFS that also recomputed
// every SR3 cut's coefficient from scratch (a fresh Pattern reconstruction)
// at every recursive call, instead of the incremental per-label cut state
// the root DP already uses. Verified against legacy source
// (BPPS_BP_MAPPING.cpp, DP.cpp:18-318, mckpsc-ls.cpp:2451-2571, see the
// research summarized in build_branch_groups' comment) before implementing:
// legacy carries no extra per-label branching state either -- it transforms
// the item universe once per node (contraction for Together, a conflict
// adjacency list for Different) and enforces conflicts purely by pruning a
// label's still-reachable extensions, exactly what this function does.
std::vector<PricingResult> FloatingRootPricer::price_label_setting_with_branching_and_sr3(
    const Instance& instance, const std::vector<double>& duals,
    const BranchingState& branching, const std::vector<Sr3Cut>& cuts,
    std::size_t max_candidates) const {
  if (branching.constraints().empty()) {
    return cuts.empty() ? price_label_setting(instance, duals, max_candidates)
                        : price_label_setting_with_sr3(instance, duals, cuts, max_candidates);
  }
  if (max_candidates == 0) return {};
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }

  const BranchGroups groups = build_branch_groups(instance, duals, branching);
  const auto element_count = groups.elements.size();
  const auto k = cuts.size();

  // Assign a bitmask slot only to elements that actually participate in a
  // Different conflict; most elements never do, so the common case (few or
  // no active conflicts on a node) costs nothing beyond the load/cut-state
  // key already used for SR3 -- matching legacy's fast path, which skips all
  // conflict bookkeeping entirely when n_conflict==0 (DP.cpp:670-681).
  std::vector<int> conflict_slot(element_count, -1);
  std::size_t slot_count = 0;
  for (std::size_t element = 0; element < element_count; ++element) {
    if (!groups.conflicts[element].empty()) conflict_slot[element] = static_cast<int>(slot_count++);
  }

  constexpr std::size_t kMaxConflictSlots = 40;
  constexpr std::size_t kMaxCuts = 40;
  if (slot_count > kMaxConflictSlots || k > kMaxCuts) {
    // Exact fallback for a node with more simultaneous conflicts/cuts than
    // the packed state can hold. Still correct, just the pre-fix DFS cost;
    // expected to be unreachable in practice (branching depth on real BPP
    // trees is structurally small -- see the legacy research this function's
    // header comment cites), kept only so a pathological node can never
    // silently mis-price instead of just running slowly.
    PricingResult single = cuts.empty() ? price(instance, duals, branching)
                                        : price_with_branching_and_sr3(instance, duals, branching, cuts);
    std::vector<PricingResult> fallback;
    if (single.pattern.has_value() && single.reduced_cost < 0.0) fallback.push_back(std::move(single));
    return fallback;
  }

  std::vector<std::uint64_t> conflict_bits(element_count, 0);
  for (std::size_t element = 0; element < element_count; ++element) {
    for (int other : groups.conflicts[element]) {
      const int slot = conflict_slot[static_cast<std::size_t>(other)];
      if (slot >= 0) conflict_bits[element] |= (1ULL << slot);
    }
  }

  // Every cut a given element participates in (union over its member items):
  // a Together-contraction can pull more than one triplet member into the
  // same element at once, unlike the item-at-a-time root SR3 DP.
  std::vector<std::vector<std::size_t>> element_cuts(element_count);
  if (k > 0) {
    std::vector<std::vector<std::size_t>> item_cuts(instance.item_count());
    for (std::size_t cut = 0; cut < k; ++cut) {
      item_cuts[static_cast<std::size_t>(cuts[cut].first)].push_back(cut);
      item_cuts[static_cast<std::size_t>(cuts[cut].second)].push_back(cut);
      item_cuts[static_cast<std::size_t>(cuts[cut].third)].push_back(cut);
    }
    for (std::size_t element = 0; element < element_count; ++element) {
      std::set<std::size_t> incident;
      for (int item : groups.elements[element].items) {
        for (std::size_t cut : item_cuts[static_cast<std::size_t>(item)]) incident.insert(cut);
      }
      element_cuts[element].assign(incident.begin(), incident.end());
    }
  }
  // How many of a cut's three items land inside a given element (0..3),
  // since a single Together-merge can add more than one triplet member to a
  // label in one step -- Sr3Cut::coefficient is 1 once at least two of the
  // three are present, so the count must be able to jump by more than one.
  auto matched_in_element = [&cuts](const BranchElement& element, std::size_t cut) {
    int count = 0;
    for (int item : element.items) {
      if (item == cuts[cut].first || item == cuts[cut].second || item == cuts[cut].third) ++count;
    }
    return count;
  };

  const int capacity = instance.capacity();
  std::vector<int> order;
  for (std::size_t element = 0; element < element_count; ++element) {
    const auto& current = groups.elements[element];
    // Same dual-value screening the root DP applies to raw items: a
    // non-positive-value element can never improve an unconstrained-knapsack
    // optimum, and Different conflicts only ever remove options, never force
    // an addition, so screening stays valid with conflicts present (legacy
    // performs the same aggregate-level screening after contraction, see
    // mckpsc_ls_mapping in the research this function's header cites).
    if (!current.forbidden && current.weight <= capacity && current.value > 0.0) {
      order.push_back(static_cast<int>(element));
    }
  }
  std::sort(order.begin(), order.end(), [&groups](int left, int right) {
    const auto& a = groups.elements[static_cast<std::size_t>(left)];
    const auto& b = groups.elements[static_cast<std::size_t>(right)];
    const double ar = a.value / static_cast<double>(a.weight);
    const double br = b.value / static_cast<double>(b.weight);
    if (ar != br) return ar > br;
    return left < right;
  });

  std::vector<int> weight_by_position(order.size());
  std::vector<double> value_by_position(order.size());
  for (std::size_t position = 0; position < order.size(); ++position) {
    const auto& element = groups.elements[static_cast<std::size_t>(order[position])];
    weight_by_position[position] = element.weight;
    value_by_position[position] = element.value;
  }
  const FractionalBoundTable bound_table(weight_by_position, value_by_position);
  double positive_cut_bound = 0.0;
  for (const auto& cut : cuts) positive_cut_bound += std::max(0.0, cut.dual);
  // See price_label_setting's identical margin: absorbs the fractional
  // bound's own floating-point rounding so this DP stays sound when reused
  // with bin_cost_ set to a large integer scale K.
  const double bound_margin = bin_cost_ * 1e-9 + 1e-12;

  struct PathNode { int element; int previous; };
  std::vector<PathNode> nodes;
  struct LabelInfo { double value; int path; };
  std::unordered_map<BranchKey, LabelInfo, BranchKeyHash, BranchKeyEq> labels;
  labels[{0, 0, 0}] = {0.0, -1};

  std::vector<PricingResult> candidates;
  std::set<std::vector<int>> seen;
  auto reconstruct = [&](int node_index) {
    std::vector<int> items;
    while (node_index >= 0) {
      const auto& element = groups.elements[static_cast<std::size_t>(nodes[static_cast<std::size_t>(node_index)].element)];
      items.insert(items.end(), element.items.begin(), element.items.end());
      node_index = nodes[static_cast<std::size_t>(node_index)].previous;
    }
    std::sort(items.begin(), items.end());
    return items;
  };
  auto consider = [&](double value, int node_index) {
    const double reduced_cost = bin_cost_ - value;
    if (reduced_cost >= 0.0) return;
    if (candidates.size() >= max_candidates &&
        reduced_cost >= candidates.back().reduced_cost) return;
    auto items = reconstruct(node_index);
    if (items.empty() || !seen.insert(items).second) return;
    Pattern candidate_pattern(instance, items);
    if (!branching.accepts(candidate_pattern)) {
      throw std::logic_error("branch-aware label-setting pricer produced an invalid pattern");
    }
    PricingResult candidate;
    candidate.dual_value = value;
    candidate.reduced_cost = reduced_cost;
    candidate.pattern.emplace(std::move(candidate_pattern));
    candidates.push_back(std::move(candidate));
    std::sort(candidates.begin(), candidates.end(),
              [](const PricingResult& left, const PricingResult& right) {
                return left.reduced_cost < right.reduced_cost;
              });
    if (candidates.size() > max_candidates) candidates.pop_back();
  };

  for (std::size_t position = 0; position < order.size(); ++position) {
    const int element_index = order[position];
    const auto& element = groups.elements[static_cast<std::size_t>(element_index)];
    const int weight = element.weight;
    const std::uint64_t own_bit = (conflict_slot[static_cast<std::size_t>(element_index)] >= 0)
                                       ? (1ULL << conflict_slot[static_cast<std::size_t>(element_index)])
                                       : 0ULL;
    const std::uint64_t forbidding_bits = conflict_bits[static_cast<std::size_t>(element_index)];
    const auto& incident_cuts = element_cuts[static_cast<std::size_t>(element_index)];

    // Permanent one-way fathoming prune, same argument as
    // price_label_setting_with_sr3: this bound only shrinks as position
    // advances, so a label failing it now fails it forever.
    for (auto it = labels.begin(); it != labels.end();) {
      if (it->second.value + bound_table.bound(position, capacity - static_cast<int>(it->first.load)) +
              positive_cut_bound + bound_margin <=
          bin_cost_) {
        it = labels.erase(it);
      } else {
        ++it;
      }
    }
    if (labels.empty()) break;

    // A snapshot freezes the labels visible before this element, so each
    // path still selects every element at most once (mirrors the
    // descending-capacity/frozen-snapshot trick the item-level DPs use for
    // the same 0/1 reason).
    const auto snapshot = labels;
    for (const auto& [key, info] : snapshot) {
      const int load = static_cast<int>(key.load);
      if (load + weight > capacity) continue;
      if ((key.conflict_mask & forbidding_bits) != 0) continue;
      const double extended_value = info.value + element.value;
      if (extended_value + bound_table.bound(position + 1, capacity - load - weight) + positive_cut_bound +
              bound_margin <=
          bin_cost_) {
        continue;
      }

      // Same parity-bit toggle as price_label_setting_with_sr3, looped once
      // per matched member instead of once per item: a Together-contracted
      // element can carry 2 or even all 3 of a cut's members in a single
      // step (matched_in_element), unlike the root DP's one-item-at-a-time
      // extensions. Looping the single-occurrence toggle `matched` times is
      // exactly equivalent to adding the members one at a time (the bonus
      // fires on whichever toggle is the 1->2 transition, same as legacy);
      // sound because each cut has exactly 3 members total, so a label can
      // never be extended past count 3 for a given cut, and the toggle
      // sequence 0->1->0->1 (counts 0,1,2,3) never revisits the 1->2 edge.
      std::uint64_t new_cut_state = key.cut_state;
      double bonus = 0.0;
      for (std::size_t cut : incident_cuts) {
        const std::uint64_t bit = 1ULL << cut;
        const int matched = matched_in_element(element, cut);
        for (int occurrence = 0; occurrence < matched; ++occurrence) {
          const bool was_primed = (new_cut_state & bit) != 0;
          new_cut_state ^= bit;
          if (was_primed) bonus += cuts[cut].dual;
        }
      }
      const double new_value = extended_value + bonus;
      const BranchKey new_key{static_cast<std::uint32_t>(load + weight), new_cut_state,
                              key.conflict_mask | own_bit};
      auto existing = labels.find(new_key);
      if (existing != labels.end() && existing->second.value >= new_value) continue;
      nodes.push_back({element_index, info.path});
      const int node_index = static_cast<int>(nodes.size()) - 1;
      labels[new_key] = {new_value, node_index};
      consider(new_value, node_index);
    }
  }
  return candidates;
}

PricingResult FloatingRootPricer::price_with_branching_and_sr3(
    const Instance& instance, const std::vector<double>& duals,
    const BranchingState& branching, const std::vector<Sr3Cut>& cuts) const {
  if (branching.constraints().empty()) return price_with_sr3(instance, duals, cuts);
  if (cuts.empty()) return price(instance, duals, branching);
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }

  const BranchGroups branch_groups = build_branch_groups(instance, duals, branching);
  const auto& groups = branch_groups.elements;
  std::vector<std::vector<bool>> conflict(groups.size(), std::vector<bool>(groups.size(), false));
  for (std::size_t left = 0; left < branch_groups.conflicts.size(); ++left) {
    for (int right : branch_groups.conflicts[left]) {
      conflict[left][static_cast<std::size_t>(right)] = true;
    }
  }

  std::vector<int> order;
  for (int group = 0; group < static_cast<int>(groups.size()); ++group) {
    const auto& current = groups[static_cast<std::size_t>(group)];
    if (!current.forbidden && current.weight <= instance.capacity()) {
      order.push_back(group);
    }
  }
  std::sort(order.begin(), order.end(), [&groups](int left, int right) {
    const auto& a = groups[static_cast<std::size_t>(left)];
    const auto& b = groups[static_cast<std::size_t>(right)];
    const double ar = a.value / static_cast<double>(a.weight);
    const double br = b.value / static_cast<double>(b.weight);
    if (ar != br) return ar > br;
    return left < right;
  });
  double positive_cut_bound = 0.0;
  for (const auto& cut : cuts) if (cut.dual > 0.0) positive_cut_bound += cut.dual;

  const int capacity = instance.capacity();
  double best_value = 0.0;
  std::vector<int> selected;
  std::vector<int> best_selected;
  std::function<void(std::size_t, int, double)> search =
      [&](std::size_t position, int load, double group_value) {
        std::vector<int> selected_items;
        for (int group : selected) {
          const auto& current = groups[static_cast<std::size_t>(group)];
          selected_items.insert(selected_items.end(), current.items.begin(), current.items.end());
        }
        double cut_value = 0.0;
        if (!selected_items.empty()) {
          Pattern current(instance, selected_items);
          for (const auto& cut : cuts) cut_value += cut.dual * cut.coefficient(current);
        }
        const double value = group_value + cut_value;
        if (value > best_value) {
          best_value = value;
          best_selected = selected;
        }
        if (position == order.size()) return;
        int remaining = capacity - load;
        double bound = group_value + positive_cut_bound;
        for (std::size_t p = position; p < order.size() && remaining > 0; ++p) {
          const auto& current = groups[static_cast<std::size_t>(order[p])];
          bool blocked = false;
          for (int chosen : selected) {
            if (conflict[static_cast<std::size_t>(order[p])][static_cast<std::size_t>(chosen)]) {
              blocked = true;
              break;
            }
          }
          if (blocked) continue;
          if (current.value <= 0.0) continue;
          if (current.weight <= remaining) {
            remaining -= current.weight;
            bound += current.value;
          } else {
            bound += current.value * (static_cast<double>(remaining) / current.weight);
            break;
          }
        }
        if (bound <= best_value + 1e-12) return;
        const int group = order[position];
        const auto& current = groups[static_cast<std::size_t>(group)];
        bool allowed = load + current.weight <= capacity;
        if (allowed) {
          for (int chosen : selected) {
            if (conflict[static_cast<std::size_t>(group)][static_cast<std::size_t>(chosen)]) {
              allowed = false;
              break;
            }
          }
        }
        if (allowed) {
          selected.push_back(group);
          search(position + 1, load + current.weight, group_value + current.value);
          selected.pop_back();
        }
        search(position + 1, load, group_value);
      };
  search(0, 0, 0.0);

  std::vector<int> items;
  for (int group : best_selected) {
    const auto& current = groups[static_cast<std::size_t>(group)];
    items.insert(items.end(), current.items.begin(), current.items.end());
  }
  PricingResult result;
  result.dual_value = best_value;
  result.reduced_cost = bin_cost_ - best_value;
  if (!items.empty()) {
    Pattern candidate(instance, std::move(items));
    if (!branching.accepts(candidate)) throw std::logic_error("combined pricing produced invalid branch pattern");
    result.pattern.emplace(std::move(candidate));
  }
  return result;
}

PricingResult FloatingRootPricer::price(const Instance& instance,
                                        const std::vector<double>& duals,
                                        const BranchingState& branching) const {
  if (!std::isfinite(bin_cost_) || bin_cost_ <= 0.0) {
    throw std::invalid_argument("pricing bin cost must be finite and positive");
  }
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one pricing dual is required for every item");
  }
  for (double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("pricing duals must be finite");
  }
  if (branching.constraints().empty()) return price(instance, duals);

  const BranchGroups branch_groups = build_branch_groups(instance, duals, branching);
  const auto& groups = branch_groups.elements;
  std::vector<std::vector<bool>> conflict(groups.size(), std::vector<bool>(groups.size(), false));
  for (std::size_t left = 0; left < branch_groups.conflicts.size(); ++left) {
    for (int right : branch_groups.conflicts[left]) {
      conflict[left][static_cast<std::size_t>(right)] = true;
    }
  }

  std::vector<int> order;
  for (int group = 0; group < static_cast<int>(groups.size()); ++group) {
    const auto& current = groups[static_cast<std::size_t>(group)];
    if (!current.forbidden && current.weight <= instance.capacity()) {
      order.push_back(group);
    }
  }
  std::sort(order.begin(), order.end(), [&groups](int left, int right) {
    const auto& a = groups[static_cast<std::size_t>(left)];
    const auto& b = groups[static_cast<std::size_t>(right)];
    const double ar = a.value / static_cast<double>(a.weight);
    const double br = b.value / static_cast<double>(b.weight);
    if (ar != br) return ar > br;
    return left < right;
  });

  const int capacity = instance.capacity();
  double best_value = 0.0;
  int best_load = 0;
  std::vector<int> selected;
  std::vector<int> best_selected;

  // Fractional-knapsack upper bound. It is only a pruning bound, so the
  // integral optimum remains exact even when branch restrictions are dense.
  auto upper_bound = [&](std::size_t position, int load, double value,
                         const std::vector<int>& chosen) {
    int remaining = capacity - load;
    double bound = value;
    for (std::size_t p = position; p < order.size() && remaining > 0; ++p) {
      const int group = order[p];
      bool blocked = false;
      for (int chosen_group : chosen) {
        if (conflict[static_cast<std::size_t>(group)][static_cast<std::size_t>(chosen_group)]) {
          blocked = true;
          break;
        }
      }
      if (blocked) continue;
      const auto& current = groups[static_cast<std::size_t>(group)];
      if (current.weight <= remaining) {
        remaining -= current.weight;
        bound += current.value;
      } else {
        bound += current.value * (static_cast<double>(remaining) / current.weight);
        break;
      }
    }
    return bound;
  };

  std::function<void(std::size_t, int, double)> search =
      [&](std::size_t position, int load, double value) {
        if (position == order.size()) {
          if (value > best_value) {
            best_value = value;
            best_load = load;
            best_selected = selected;
          }
          return;
        }
        if (upper_bound(position, load, value, selected) <= best_value + 1e-12) return;
        const int group = order[position];
        const auto& current = groups[static_cast<std::size_t>(group)];
        bool allowed = current.weight + load <= capacity;
        if (allowed) {
          for (int chosen_group : selected) {
            if (conflict[static_cast<std::size_t>(group)][static_cast<std::size_t>(chosen_group)]) {
              allowed = false;
              break;
            }
          }
        }
        if (allowed) {
          selected.push_back(group);
          search(position + 1, load + current.weight, value + current.value);
          selected.pop_back();
        }
        search(position + 1, load, value);
      };
  search(0, 0, 0.0);

  std::vector<int> items;
  for (int group : best_selected) {
    const auto& current = groups[static_cast<std::size_t>(group)];
    items.insert(items.end(), current.items.begin(), current.items.end());
  }
  PricingResult result;
  result.dual_value = best_value;
  result.reduced_cost = bin_cost_ - best_value;
  if (!items.empty()) {
    Pattern candidate(instance, std::move(items));
    if (!branching.accepts(candidate)) throw std::logic_error("branch-aware pricing produced an invalid pattern");
    result.pattern.emplace(std::move(candidate));
  }
  (void)best_load;
  return result;
}

}  // namespace bpp

namespace bpp {

FixedPointPricingResult FixedPointRootPricer::price(
    const Instance& instance, const std::vector<std::int64_t>& duals) const {
  if (bin_cost_ <= 0) throw std::invalid_argument("fixed-point bin cost must be positive");
  if (duals.size() != instance.item_count()) {
    throw std::invalid_argument("one fixed-point dual is required for every item");
  }

  const int capacity = instance.capacity();
  const auto max_value = std::numeric_limits<std::int64_t>::max();
  std::vector<std::int64_t> best(static_cast<std::size_t>(capacity + 1), 0);
  std::vector<std::vector<bool>> take(
      instance.item_count(), std::vector<bool>(static_cast<std::size_t>(capacity + 1), false));

  for (std::size_t item = 0; item < instance.item_count(); ++item) {
    const int weight = instance.weights()[item];
    for (int remaining = capacity; remaining >= weight; --remaining) {
      const auto previous = best[static_cast<std::size_t>(remaining - weight)];
      const auto value = duals[item];
      if ((value > 0 && previous > max_value - value) ||
          (value < 0 && previous < std::numeric_limits<std::int64_t>::min() - value)) {
        throw std::overflow_error("fixed-point pricing value overflow");
      }
      const auto candidate = previous + value;
      if (candidate > best[static_cast<std::size_t>(remaining)]) {
        best[static_cast<std::size_t>(remaining)] = candidate;
        take[item][static_cast<std::size_t>(remaining)] = true;
      }
    }
  }

  int best_capacity = 0;
  for (int used_capacity = 1; used_capacity <= capacity; ++used_capacity) {
    if (best[static_cast<std::size_t>(used_capacity)] > best[static_cast<std::size_t>(best_capacity)]) {
      best_capacity = used_capacity;
    }
  }
  std::vector<int> items;
  int remaining = best_capacity;
  for (std::size_t item = instance.item_count(); item-- > 0;) {
    if (take[item][static_cast<std::size_t>(remaining)]) {
      items.push_back(static_cast<int>(item));
      remaining -= instance.weights()[item];
    }
  }

  FixedPointPricingResult result;
  result.dual_value = best[static_cast<std::size_t>(best_capacity)];
  if (result.dual_value < std::numeric_limits<std::int64_t>::min() + bin_cost_) {
    throw std::overflow_error("fixed-point reduced-cost overflow");
  }
  result.reduced_cost = bin_cost_ - result.dual_value;
  if (!items.empty()) result.pattern.emplace(instance, std::move(items));
  return result;
}

std::vector<PricingResult> price_scaled_integer_with_sr3(
    const Instance& instance, const std::vector<std::int64_t>& scaled_duals,
    std::int64_t scale, const std::vector<Sr3Cut>& cuts,
    std::size_t max_candidates) {
  if (scale <= 0) throw std::invalid_argument("price_scaled_integer_with_sr3 scale must be positive");
  // 2^53 is the largest integer double arithmetic represents exactly;
  // requiring every individual scaled dual to stay well under it (a
  // generous margin, since the DP only ever sums a handful of them per
  // pattern -- bounded by items-per-pattern, not item count) is what keeps
  // the reused floating DP's value/reduced_cost arithmetic exact, not just
  // approximately so.
  constexpr std::int64_t kMaxExactDouble = 1LL << 53;
  std::vector<double> duals(scaled_duals.size());
  for (std::size_t i = 0; i < scaled_duals.size(); ++i) {
    if (scaled_duals[i] <= -kMaxExactDouble || scaled_duals[i] >= kMaxExactDouble) {
      throw std::overflow_error("scaled dual too large for exact double pricing");
    }
    duals[i] = static_cast<double>(scaled_duals[i]);
  }
  for (const auto& cut : cuts) {
    if (cut.dual <= -static_cast<double>(kMaxExactDouble) ||
        cut.dual >= static_cast<double>(kMaxExactDouble)) {
      throw std::overflow_error("scaled cut dual too large for exact double pricing");
    }
  }
  const FloatingRootPricer scaled_pricer(static_cast<double>(scale));
  return scaled_pricer.price_label_setting_with_sr3(instance, duals, cuts, max_candidates);
}

std::vector<PricingResult> price_scaled_integer_with_branching_and_sr3(
    const Instance& instance, const std::vector<std::int64_t>& scaled_duals,
    std::int64_t scale, const BranchingState& branching, const std::vector<Sr3Cut>& cuts,
    std::size_t max_candidates) {
  if (branching.constraints().empty()) {
    return price_scaled_integer_with_sr3(instance, scaled_duals, scale, cuts, max_candidates);
  }
  if (scale <= 0) {
    throw std::invalid_argument("price_scaled_integer_with_branching_and_sr3 scale must be positive");
  }
  constexpr std::int64_t kMaxExactDouble = 1LL << 53;
  std::vector<double> duals(scaled_duals.size());
  for (std::size_t i = 0; i < scaled_duals.size(); ++i) {
    if (scaled_duals[i] <= -kMaxExactDouble || scaled_duals[i] >= kMaxExactDouble) {
      throw std::overflow_error("scaled dual too large for exact double pricing");
    }
    duals[i] = static_cast<double>(scaled_duals[i]);
  }
  for (const auto& cut : cuts) {
    if (cut.dual <= -static_cast<double>(kMaxExactDouble) ||
        cut.dual >= static_cast<double>(kMaxExactDouble)) {
      throw std::overflow_error("scaled cut dual too large for exact double pricing");
    }
  }
  const FloatingRootPricer scaled_pricer(static_cast<double>(scale));
  return scaled_pricer.price_label_setting_with_branching_and_sr3(instance, duals, branching, cuts,
                                                                   max_candidates);
}

}  // namespace bpp
