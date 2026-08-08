#pragma once

#include "bpp/instance.hpp"
#include "bpp/pattern.hpp"
#include "bpp/cuts.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace bpp {

// Floating restricted master for the set-covering formulation, backed by
// Gurobi instead of CPLEX. Same public contract as CplexRmp (see its doc
// comment) -- the two are interchangeable RMP backends selected at build
// time (BPP_ENABLE_CPLEX/BPP_ENABLE_GUROBI) and, when both are built, at
// runtime via `bpp-solve --solver cplex|gurobi`. Gurobi headers stay
// private to the implementation.
class GurobiRmp {
 public:
  explicit GurobiRmp(const Instance& instance, std::vector<Sr3Cut> cuts = {});
  ~GurobiRmp();
  GurobiRmp(GurobiRmp&&) noexcept;
  GurobiRmp& operator=(GurobiRmp&&) noexcept;
  GurobiRmp(const GurobiRmp&) = delete;
  GurobiRmp& operator=(const GurobiRmp&) = delete;

  void add_pattern(const Pattern& pattern);
  // Appends one SR3 row to the live LP instead of rebuilding it, so a
  // subsequent solve() warm-starts from the previous basis. `patterns` must
  // list every column already added, in the same order as add_pattern was
  // called, so the new row's coefficients can be set for existing columns.
  void add_cut(const Sr3Cut& cut, const std::vector<Pattern>& patterns);
  void solve();
  double objective_value() const noexcept;
  const std::vector<double>& duals() const noexcept;
  const std::vector<double>& sr3_duals() const noexcept;
  const std::vector<Sr3Cut>& sr3_cuts() const noexcept;
  const std::vector<double>& primal_values() const noexcept;
  std::size_t pattern_count() const noexcept;

  // Same contract as CplexRmp::solve_mip_at_most (see its doc comment):
  // turns the current pool into a genuine 0/1 covering MIP with an added
  // "at most max_bins patterns" row, solved by Gurobi's own branch-and-cut.
  // Returns the selected pattern indices, or std::nullopt if Gurobi proves
  // no such selection exists (a complete optimality proof for max_bins+1,
  // independent of the LP/dual bound).
  std::optional<std::vector<std::size_t>> solve_mip_at_most(std::size_t max_bins);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bpp
