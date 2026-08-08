#pragma once

#include "bpp/cplex_rmp.hpp"
#include "bpp/gurobi_rmp.hpp"

#include <variant>

namespace bpp {

// Which floating-point LP backend solves the restricted master. Both are
// alternative implementations of the same historical role (legacy's
// PARAM_SOLVER/PARAM_PRICER==1 CPLEX-vs-Gurobi choice); CPLEX is the
// default everywhere in this codebase, matching every historical
// ANI-comparison parameter file. See official-reference-BCCF/README.md for
// why Gurobi exists in this algorithm's history at all: the paper's own
// code uses it as an intermediate-precision LP solver alongside CPLEX and
// SoPlex, not purely as a CPLEX substitute -- this codebase only restores
// it as an alternative RMP backend, not that three-tier scheme.
enum class LpBackend { Cplex, Gurobi };

// Backend-agnostic wrapper around CplexRmp/GurobiRmp, selected at
// construction time. The two concrete classes share an identical public
// contract by design (see either's doc comment); this just forwards to
// whichever was selected, so the column-generation loop needs no
// backend-specific branch beyond picking one at construction. Both classes
// are always compiled in (as throwing stubs when their backend's
// BPP_ENABLE_* flag is off, see cplex_rmp.cpp/gurobi_rmp.cpp), so
// std::variant<CplexRmp, GurobiRmp> is always a valid type regardless of
// which backend(s) a given build actually has -- constructing the disabled
// one simply throws at runtime, the same failure mode as calling any other
// method on a not-built-in backend.
class MasterRmp {
 public:
  MasterRmp(LpBackend backend, const Instance& instance, std::vector<Sr3Cut> cuts = {});

  void add_pattern(const Pattern& pattern);
  void add_cut(const Sr3Cut& cut, const std::vector<Pattern>& patterns);
  void solve();
  bool solve_if_feasible();
  void add_pattern_count_upper_bound(std::size_t max_bins);
  double objective_value() const noexcept;
  const std::vector<double>& duals() const noexcept;
  const std::vector<double>& sr3_duals() const noexcept;
  const std::vector<Sr3Cut>& sr3_cuts() const noexcept;
  const std::vector<double>& primal_values() const noexcept;
  std::size_t pattern_count() const noexcept;
  // See CplexRmp::set_pattern_eligible / GurobiRmp::set_pattern_eligible.
  void set_pattern_eligible(const std::vector<std::size_t>& indices, bool eligible);

 private:
  std::variant<CplexRmp, GurobiRmp> backend_;
};

}  // namespace bpp
