#include "bpp/cplex_rmp.hpp"

#include <stdexcept>
#include <algorithm>

#ifdef BPP_HAS_CPLEX

#include <ilcplex/cplex.h>

#include <stdexcept>
#include <utility>

namespace bpp {

struct CplexRmp::Impl {
  const Instance* instance;
  std::vector<Sr3Cut> cuts;
  CPXENVptr env = nullptr;
  CPXLPptr lp = nullptr;
  std::vector<double> duals;
  std::vector<double> sr3_duals;
  std::vector<double> primals;
  double objective = 0.0;
  std::size_t patterns = 0;

  explicit Impl(const Instance& value, std::vector<Sr3Cut> active_cuts)
      : instance(&value), cuts(std::move(active_cuts)), duals(value.item_count(), 0.0),
        sr3_duals(cuts.size(), 0.0) {
    int status = 0;
    env = CPXopenCPLEX(&status);
    if (env == nullptr || status != 0) throw std::runtime_error("CPLEX environment creation failed");
    lp = CPXcreateprob(env, &status, "bpp-rmp");
    if (lp == nullptr || status != 0) throw std::runtime_error("CPLEX RMP creation failed");
    if (CPXchgobjsen(env, lp, CPX_MIN) != 0) throw std::runtime_error("CPLEX objective setup failed");
    // Legacy (BPPS_BP_MASTER.cpp:1382-1383, PARAM_SIMPLEX=1 in the ANI
    // parameter files) forces primal simplex and a single thread for the
    // RMP. Column generation re-solves after adding only a handful of
    // columns each iteration, which is exactly primal simplex's best case
    // for a warm start; CPLEX's automatic method/thread selection instead
    // measurably favored a more expensive strategy on larger instances (see
    // docs/STATUS.md, ANI-402 checkpoint: ~48ms/resolve here vs. legacy's
    // single-digit-ms).
    CPXsetintparam(env, CPX_PARAM_LPMETHOD, CPX_ALG_PRIMAL);
    CPXsetintparam(env, CPX_PARAM_THREADS, 1);
    std::vector<double> rhs(value.item_count() + cuts.size(), 1.0);
    std::vector<char> sense(value.item_count() + cuts.size(), 'G');
    for (std::size_t row = value.item_count(); row < sense.size(); ++row) sense[row] = 'L';
    if (CPXaddrows(env, lp, 0, static_cast<int>(value.item_count() + cuts.size()),
                   0, rhs.data(), sense.data(), nullptr, nullptr, nullptr, nullptr, nullptr) != 0) {
      throw std::runtime_error("CPLEX RMP row creation failed");
    }
  }

  ~Impl() {
    if (lp != nullptr) CPXfreeprob(env, &lp);
    if (env != nullptr) CPXcloseCPLEX(&env);
  }
};

CplexRmp::CplexRmp(const Instance& instance, std::vector<Sr3Cut> cuts)
    : impl_(std::make_unique<Impl>(instance, std::move(cuts))) {}
CplexRmp::~CplexRmp() = default;
CplexRmp::CplexRmp(CplexRmp&&) noexcept = default;
CplexRmp& CplexRmp::operator=(CplexRmp&&) noexcept = default;

void CplexRmp::add_pattern(const Pattern& pattern) {
  std::vector<int> indices;
  std::vector<double> values;
  indices.reserve(pattern.items().size());
  values.reserve(pattern.items().size());
  for (int item : pattern.items()) {
    indices.push_back(item);
    values.push_back(1.0);
  }
  for (std::size_t cut = 0; cut < impl_->cuts.size(); ++cut) {
    const int coefficient = impl_->cuts[cut].coefficient(pattern);
    if (coefficient != 0) {
      indices.push_back(static_cast<int>(impl_->instance->item_count() + cut));
      values.push_back(static_cast<double>(coefficient));
    }
  }
  const double objective = 1.0;
  const double lower = 0.0;
  const double upper = CPX_INFBOUND;
  const int column_start = 0;
  if (CPXaddcols(impl_->env, impl_->lp, 1, static_cast<int>(indices.size()), &objective,
                 &column_start, indices.data(), values.data(), &lower, &upper,
                 nullptr) != 0) {
    throw std::runtime_error("CPLEX pattern column creation failed");
  }
  ++impl_->patterns;
  impl_->primals.resize(impl_->patterns, 0.0);
}

void CplexRmp::add_cut(const Sr3Cut& cut, const std::vector<Pattern>& patterns) {
  if (patterns.size() != impl_->patterns) {
    throw std::invalid_argument("add_cut requires every existing column, in insertion order");
  }
  const double rhs = 1.0;
  const char sense = 'L';
  if (CPXnewrows(impl_->env, impl_->lp, 1, &rhs, &sense, nullptr, nullptr) != 0) {
    throw std::runtime_error("CPLEX SR3 row creation failed");
  }
  const int row = static_cast<int>(impl_->instance->item_count() + impl_->cuts.size());
  std::vector<int> rows;
  std::vector<int> cols;
  std::vector<double> values;
  for (std::size_t column = 0; column < patterns.size(); ++column) {
    const int coefficient = cut.coefficient(patterns[column]);
    if (coefficient != 0) {
      rows.push_back(row);
      cols.push_back(static_cast<int>(column));
      values.push_back(static_cast<double>(coefficient));
    }
  }
  if (!rows.empty() &&
      CPXchgcoeflist(impl_->env, impl_->lp, static_cast<int>(rows.size()), rows.data(),
                     cols.data(), values.data()) != 0) {
    throw std::runtime_error("CPLEX SR3 row coefficient assignment failed");
  }
  impl_->cuts.push_back(cut);
  impl_->sr3_duals.push_back(0.0);
}

void CplexRmp::solve() {
  if (!solve_if_feasible()) {
    throw std::runtime_error("CPLEX RMP is infeasible");
  }
}

bool CplexRmp::solve_if_feasible() {
  if (impl_->patterns == 0) throw std::logic_error("cannot solve an RMP without patterns");
  if (CPXlpopt(impl_->env, impl_->lp) != 0) throw std::runtime_error("CPLEX RMP solve failed");
  const int status = CPXgetstat(impl_->env, impl_->lp);
  if (status == CPX_STAT_INFEASIBLE || status == CPX_STAT_INForUNBD) return false;
  if (status != CPX_STAT_OPTIMAL) {
    throw std::runtime_error("CPLEX RMP did not reach optimality (status " + std::to_string(status) + ")");
  }
  std::vector<double> all_duals(impl_->duals.size() + impl_->sr3_duals.size(), 0.0);
  if (CPXgetobjval(impl_->env, impl_->lp, &impl_->objective) != 0 ||
      CPXgetpi(impl_->env, impl_->lp, all_duals.data(), 0,
               static_cast<int>(all_duals.size()) - 1) != 0 ||
      CPXgetx(impl_->env, impl_->lp, impl_->primals.data(), 0,
              static_cast<int>(impl_->primals.size()) - 1) != 0) {
    throw std::runtime_error("CPLEX RMP solution extraction failed");
  }
  std::copy(all_duals.begin(), all_duals.begin() + static_cast<std::ptrdiff_t>(impl_->duals.size()),
            impl_->duals.begin());
  std::copy(all_duals.begin() + static_cast<std::ptrdiff_t>(impl_->duals.size()), all_duals.end(),
            impl_->sr3_duals.begin());
  return true;
}

void CplexRmp::add_pattern_count_upper_bound(std::size_t max_bins) {
  if (impl_->patterns == 0) throw std::logic_error("cannot constrain an RMP without patterns");
  const double rhs = static_cast<double>(max_bins);
  const char sense = 'L';
  if (CPXnewrows(impl_->env, impl_->lp, 1, &rhs, &sense, nullptr, nullptr) != 0) {
    throw std::runtime_error("CPLEX SAFE_MIP_SOL bin-count row creation failed");
  }
  std::vector<int> columns(impl_->patterns);
  std::vector<double> ones(impl_->patterns, 1.0);
  for (std::size_t i = 0; i < impl_->patterns; ++i) columns[i] = static_cast<int>(i);
  const int row = static_cast<int>(impl_->instance->item_count() + impl_->cuts.size());
  std::vector<int> rows(impl_->patterns, row);
  if (CPXchgcoeflist(impl_->env, impl_->lp, static_cast<int>(impl_->patterns), rows.data(),
                     columns.data(), ones.data()) != 0) {
    throw std::runtime_error("CPLEX SAFE_MIP_SOL bin-count coefficients failed");
  }
}

std::optional<std::vector<std::size_t>> CplexRmp::solve_mip_at_most(std::size_t max_bins) {
  if (impl_->patterns == 0) throw std::logic_error("cannot solve an RMP without patterns");
  std::vector<int> indices(impl_->patterns);
  std::vector<char> types(impl_->patterns, 'B');
  for (std::size_t i = 0; i < impl_->patterns; ++i) indices[i] = static_cast<int>(i);
  if (CPXchgctype(impl_->env, impl_->lp, static_cast<int>(impl_->patterns), indices.data(),
                  types.data()) != 0) {
    throw std::runtime_error("CPLEX MIP column-type change failed");
  }
  const double rhs = static_cast<double>(max_bins);
  const char sense = 'L';
  if (CPXnewrows(impl_->env, impl_->lp, 1, &rhs, &sense, nullptr, nullptr) != 0) {
    throw std::runtime_error("CPLEX MIP bin-count row creation failed");
  }
  const int row = static_cast<int>(impl_->instance->item_count() + impl_->cuts.size());
  std::vector<int> rows(impl_->patterns, row);
  std::vector<double> ones(impl_->patterns, 1.0);
  if (CPXchgcoeflist(impl_->env, impl_->lp, static_cast<int>(impl_->patterns), rows.data(),
                     indices.data(), ones.data()) != 0) {
    throw std::runtime_error("CPLEX MIP bin-count row coefficient assignment failed");
  }
  // Single thread, matching the RMP's own LPMETHOD/THREADS setup. Unlike
  // the LP-relaxation solves elsewhere in this codebase, this is a genuine
  // NP-hard covering MIP -- proving infeasibility for a hard instance can
  // take a long time even over a modest pool, so this needs an explicit
  // time limit (the caller treats "did not finish" as "could not certify
  // this way", not as a crash or an infinite hang).
  CPXsetintparam(impl_->env, CPX_PARAM_THREADS, 1);
  CPXsetdblparam(impl_->env, CPX_PARAM_TILIM, 20.0);
  if (CPXmipopt(impl_->env, impl_->lp) != 0) throw std::runtime_error("CPLEX MIP solve failed");
  const int status = CPXgetstat(impl_->env, impl_->lp);
  if (status == CPXMIP_INFEASIBLE || status == CPXMIP_INForUNBD) {
    return std::nullopt;
  }
  if (status != CPXMIP_OPTIMAL && status != CPXMIP_OPTIMAL_TOL) {
    throw std::runtime_error("CPLEX MIP did not reach a conclusive status (status " +
                             std::to_string(status) + ")");
  }
  std::vector<double> x(impl_->patterns, 0.0);
  if (CPXgetmipx(impl_->env, impl_->lp, x.data(), 0, static_cast<int>(impl_->patterns) - 1) != 0) {
    throw std::runtime_error("CPLEX MIP solution extraction failed");
  }
  std::vector<std::size_t> selected;
  for (std::size_t i = 0; i < impl_->patterns; ++i) {
    if (x[i] > 0.5) selected.push_back(i);
  }
  return selected;
}

void CplexRmp::set_pattern_eligible(const std::vector<std::size_t>& indices, bool eligible) {
  if (indices.empty()) return;
  std::vector<int> columns(indices.size());
  std::vector<char> bound_type(indices.size(), 'U');
  std::vector<double> bound_value(indices.size(), eligible ? CPX_INFBOUND : 0.0);
  for (std::size_t i = 0; i < indices.size(); ++i) columns[i] = static_cast<int>(indices[i]);
  if (CPXchgbds(impl_->env, impl_->lp, static_cast<int>(columns.size()), columns.data(),
               bound_type.data(), bound_value.data()) != 0) {
    throw std::runtime_error("CPLEX pattern eligibility change failed");
  }
}

double CplexRmp::objective_value() const noexcept { return impl_->objective; }
const std::vector<double>& CplexRmp::duals() const noexcept { return impl_->duals; }
const std::vector<double>& CplexRmp::sr3_duals() const noexcept { return impl_->sr3_duals; }
const std::vector<Sr3Cut>& CplexRmp::sr3_cuts() const noexcept { return impl_->cuts; }
const std::vector<double>& CplexRmp::primal_values() const noexcept { return impl_->primals; }
std::size_t CplexRmp::pattern_count() const noexcept { return impl_->patterns; }

}  // namespace bpp

#else

namespace bpp {
struct CplexRmp::Impl {};
CplexRmp::CplexRmp(const Instance&, std::vector<Sr3Cut>) { throw std::runtime_error("BPP was built without CPLEX"); }
CplexRmp::~CplexRmp() = default;
CplexRmp::CplexRmp(CplexRmp&&) noexcept = default;
CplexRmp& CplexRmp::operator=(CplexRmp&&) noexcept = default;
void CplexRmp::add_pattern(const Pattern&) { throw std::runtime_error("BPP was built without CPLEX"); }
void CplexRmp::add_cut(const Sr3Cut&, const std::vector<Pattern>&) { throw std::runtime_error("BPP was built without CPLEX"); }
void CplexRmp::solve() { throw std::runtime_error("BPP was built without CPLEX"); }
bool CplexRmp::solve_if_feasible() { throw std::runtime_error("BPP was built without CPLEX"); }
void CplexRmp::add_pattern_count_upper_bound(std::size_t) { throw std::runtime_error("BPP was built without CPLEX"); }
double CplexRmp::objective_value() const noexcept { return 0.0; }
const std::vector<double>& CplexRmp::duals() const noexcept { static const std::vector<double> empty; return empty; }
const std::vector<double>& CplexRmp::sr3_duals() const noexcept { static const std::vector<double> empty; return empty; }
const std::vector<Sr3Cut>& CplexRmp::sr3_cuts() const noexcept { static const std::vector<Sr3Cut> empty; return empty; }
const std::vector<double>& CplexRmp::primal_values() const noexcept { static const std::vector<double> empty; return empty; }
std::size_t CplexRmp::pattern_count() const noexcept { return 0; }
std::optional<std::vector<std::size_t>> CplexRmp::solve_mip_at_most(std::size_t) {
  throw std::runtime_error("BPP was built without CPLEX");
}
void CplexRmp::set_pattern_eligible(const std::vector<std::size_t>&, bool) {
  throw std::runtime_error("BPP was built without CPLEX");
}
}  // namespace bpp

#endif
