#pragma once

#include "bpp/instance.hpp"
#include "bpp/pattern.hpp"
#include "bpp/cuts.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace bpp {

// Floating restricted master for the set-covering formulation. CPLEX headers
// stay private to the implementation; users of the public API only see BPP
// domain objects.
class CplexRmp {
 public:
  explicit CplexRmp(const Instance& instance, std::vector<Sr3Cut> cuts = {});
  ~CplexRmp();
  CplexRmp(CplexRmp&&) noexcept;
  CplexRmp& operator=(CplexRmp&&) noexcept;
  CplexRmp(const CplexRmp&) = delete;
  CplexRmp& operator=(const CplexRmp&) = delete;

  void add_pattern(const Pattern& pattern);
  // Appends one SR3 row to the live LP instead of rebuilding it, so a
  // subsequent solve() warm-starts from the previous basis. `patterns` must
  // list every column already added, in the same order as add_pattern was
  // called, so the new row's coefficients can be set for existing columns.
  void add_cut(const Sr3Cut& cut, const std::vector<Pattern>& patterns);
  void solve();
  // As solve(), but reports an infeasible LP as false.  SAFE_MIP_SOL uses
  // infeasibility to fathom a Ryan--Foster node; operational failures still
  // throw, so they can never be mistaken for a proof.
  bool solve_if_feasible();
  // Adds the historical SAFE_MIP_SOL row sum_p x_p <= max_bins to the
  // current fixed pool.  It is deliberately an LP row: integrality is
  // imposed by the Ryan--Foster tree, not by CPLEX's MIP engine.
  void add_pattern_count_upper_bound(std::size_t max_bins);
  double objective_value() const noexcept;
  const std::vector<double>& duals() const noexcept;
  const std::vector<double>& sr3_duals() const noexcept;
  const std::vector<Sr3Cut>& sr3_cuts() const noexcept;
  const std::vector<double>& primal_values() const noexcept;
  std::size_t pattern_count() const noexcept;

  // Legacy SAFE_MIP_SOL (DP_POP.cpp:910-978): solves the *current pool* as
  // a genuine 0/1 covering MIP (not the LP relaxation), with an added row
  // requiring at most `max_bins` patterns selected. Returns the selected
  // pattern indices when feasible; std::nullopt when CPLEX's exact
  // branch-and-cut proves no such selection exists -- a combinatorial
  // certificate, independent of (and not weakened by) any floating-point
  // LP dual bound. Only meaningful once the pool already contains every
  // pattern that could possibly matter (see populate_root_columns's
  // enumeration threshold); this call does not itself enumerate anything.
  // One-shot: leaves the LP's column types changed to binary and adds a
  // permanent row, so the instance should not be reused for further LP
  // solving, nor called a second time with a different max_bins on the
  // same instance (the second call's row coefficients silently land on
  // the wrong row index, since row numbering assumes exactly one prior
  // call) -- construct a fresh Rmp per attempt instead, exactly as
  // try_safe_mip_certification does.
  std::optional<std::vector<std::size_t>> solve_mip_at_most(std::size_t max_bins);

  // Toggles a set of existing pattern columns in or out of the LP by their
  // upper bound (0.0 when ineligible, unbounded when eligible again), rather
  // than adding/removing columns. This is what lets restricted_branch_and_bound
  // (column_generation.cpp) explore Ryan-Foster branches over a *fixed* pool
  // by re-solving the SAME persistent RMP with different columns masked out,
  // instead of rebuilding a fresh RMP (and re-adding every pattern) at every
  // node -- the latter was measured to make a real branch-and-bound tree
  // both far too slow and prone to crashing (see docs/STATUS.md). `indices`
  // must reference columns already added via add_pattern.
  void set_pattern_eligible(const std::vector<std::size_t>& indices, bool eligible);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bpp
