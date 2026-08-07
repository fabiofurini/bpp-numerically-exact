#include "bpp/heuristics.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace bpp {
namespace {

std::vector<int> weight_order(const Instance& instance) {
  std::vector<int> order(instance.item_count());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&instance](int left, int right) {
    if (instance.weights()[left] != instance.weights()[right]) {
      return instance.weights()[left] > instance.weights()[right];
    }
    return left < right;
  });
  return order;
}

}  // namespace

Solution solve_first_fit_decreasing(const Instance& instance) {
  Solution solution(&instance);
  std::vector<int> loads;
  std::vector<std::vector<int>> bins;
  for (const int item : weight_order(instance)) {
    auto destination = std::find_if(loads.begin(), loads.end(), [&instance, item](int load) {
      return load + instance.weights()[item] <= instance.capacity();
    });
    if (destination == loads.end()) {
      loads.push_back(instance.weights()[item]);
      bins.push_back({item});
    } else {
      const auto bin = static_cast<std::size_t>(destination - loads.begin());
      *destination += instance.weights()[item];
      bins[bin].push_back(item);
    }
  }
  for (auto& bin : bins) solution.add_bin(std::move(bin));
  return solution;
}

Solution solve_best_fit_decreasing(const Instance& instance) {
  Solution solution(&instance);
  std::vector<int> loads;
  std::vector<std::vector<int>> bins;
  for (const int item : weight_order(instance)) {
    std::size_t selected = loads.size();
    int smallest_slack = instance.capacity() + 1;
    for (std::size_t bin = 0; bin < loads.size(); ++bin) {
      const int slack = instance.capacity() - loads[bin] - instance.weights()[item];
      if (slack >= 0 && slack < smallest_slack) {
        smallest_slack = slack;
        selected = bin;
      }
    }
    if (selected == loads.size()) {
      loads.push_back(instance.weights()[item]);
      bins.push_back({item});
    } else {
      loads[selected] += instance.weights()[item];
      bins[selected].push_back(item);
    }
  }
  for (auto& bin : bins) solution.add_bin(std::move(bin));
  return solution;
}

Solution round_master_solution(const Instance& instance,
                               const std::vector<Pattern>& patterns,
                               const std::vector<double>& column_values,
                               double selection_threshold) {
  if (patterns.size() != column_values.size()) {
    throw std::invalid_argument("one master value is required for every pattern");
  }
  if (!std::isfinite(selection_threshold) || selection_threshold < 0.0 || selection_threshold > 1.0) {
    throw std::invalid_argument("pattern selection threshold must be in [0, 1]");
  }
  std::vector<std::size_t> order(patterns.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&column_values](std::size_t left, std::size_t right) {
    return column_values[left] > column_values[right];
  });
  std::vector<bool> covered(instance.item_count(), false);
  Solution rounded(&instance);
  for (const auto column : order) {
    if (column_values[column] + 1e-12 < selection_threshold) break;
    bool compatible = true;
    for (const int item : patterns[column].items()) {
      if (covered[static_cast<std::size_t>(item)]) {
        compatible = false;
        break;
      }
    }
    if (!compatible) continue;
    rounded.add_bin(patterns[column].items());
    for (const int item : patterns[column].items()) covered[static_cast<std::size_t>(item)] = true;
  }

  std::vector<int> residual;
  for (std::size_t item = 0; item < covered.size(); ++item) {
    if (!covered[item]) residual.push_back(static_cast<int>(item));
  }
  auto fallback = solve_best_fit_decreasing(Instance(instance.capacity(), [&instance, &residual]() {
    std::vector<int> weights;
    weights.reserve(residual.size());
    for (const int item : residual) weights.push_back(instance.weights()[static_cast<std::size_t>(item)]);
    return weights;
  }()));
  for (const auto& bin : fallback.bins()) {
    std::vector<int> original;
    original.reserve(bin.items.size());
    for (const int local : bin.items) original.push_back(residual[static_cast<std::size_t>(local)]);
    rounded.add_bin(std::move(original));
  }
  return rounded;
}

Solution dive_master_solution(const Instance& instance,
                              const std::vector<Pattern>& patterns,
                              const std::vector<double>& column_values) {
  if (patterns.size() != column_values.size()) {
    throw std::invalid_argument("one master value is required for every pattern");
  }
  // A single pass over patterns sorted by column value descending, greedily
  // keeping every pattern that does not conflict with what is already
  // covered -- the same "one sorted scan, no rescanning" shape as the
  // historical LP-heuristic passes (BPPS_BP_LP_HEUR.cpp load_sol_1/2/3,
  // each O(patterns) once) instead of repeatedly rescanning the whole pool
  // once per selected bin (which made this O(bins x patterns) and was the
  // dominant cost on large instances; see docs/STATUS.md). round_master_solution
  // already has this shape with an explicit selection_threshold; this
  // differs only in accepting any positive value (no threshold) so the two
  // heuristics can still disagree and give the caller two independent
  // incumbent candidates, mirroring the historical load_sol_1 vs load_sol_2/3
  // split (plain greedy vs near-integral-only).
  std::vector<std::size_t> order(patterns.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&column_values](std::size_t left, std::size_t right) {
    return column_values[left] > column_values[right];
  });
  std::vector<bool> covered(instance.item_count(), false);
  Solution dived(&instance);
  for (const auto column : order) {
    const double value = column_values[column];
    if (!std::isfinite(value) || value <= 1e-12) continue;
    bool compatible = true;
    for (int item : patterns[column].items()) {
      if (covered[static_cast<std::size_t>(item)]) {
        compatible = false;
        break;
      }
    }
    if (!compatible) continue;
    dived.add_bin(patterns[column].items());
    for (int item : patterns[column].items()) covered[static_cast<std::size_t>(item)] = true;
  }

  std::vector<int> residual;
  for (std::size_t item = 0; item < covered.size(); ++item) {
    if (!covered[item]) residual.push_back(static_cast<int>(item));
  }
  if (!residual.empty()) {
    std::vector<int> weights;
    weights.reserve(residual.size());
    for (int item : residual) weights.push_back(instance.weights()[static_cast<std::size_t>(item)]);
    const auto fallback = solve_best_fit_decreasing(Instance(instance.capacity(), std::move(weights)));
    for (const auto& bin : fallback.bins()) {
      std::vector<int> original;
      original.reserve(bin.items.size());
      for (int local : bin.items) original.push_back(residual[static_cast<std::size_t>(local)]);
      dived.add_bin(std::move(original));
    }
  }
  return dived;
}

}  // namespace bpp
