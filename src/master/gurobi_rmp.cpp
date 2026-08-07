#include "bpp/gurobi_rmp.hpp"

#include <stdexcept>
#include <algorithm>

#ifdef BPP_HAS_GUROBI

#include <gurobi_c.h>

#include <stdexcept>
#include <utility>

namespace bpp {

struct GurobiRmp::Impl {
  const Instance* instance;
  std::vector<Sr3Cut> cuts;
  GRBenv* env = nullptr;
  GRBmodel* model = nullptr;
  std::vector<double> duals;
  std::vector<double> sr3_duals;
  std::vector<double> primals;
  double objective = 0.0;
  std::size_t patterns = 0;

  explicit Impl(const Instance& value, std::vector<Sr3Cut> active_cuts)
      : instance(&value), cuts(std::move(active_cuts)), duals(value.item_count(), 0.0),
        sr3_duals(cuts.size(), 0.0) {
    // GRBemptyenv + set OutputFlag=0 + GRBstartenv, not GRBloadenv: Gurobi
    // prints its license banner as soon as the environment actually starts,
    // and GRBloadenv starts it immediately with no chance to silence it
    // first. This sequence sets the parameter before the environment
    // starts, so the banner never reaches stdout -- the CLI's own
    // machine-readable output is the only thing that should.
    if (GRBemptyenv(&env) != 0 || env == nullptr) {
      throw std::runtime_error("Gurobi environment creation failed");
    }
    GRBsetintparam(env, GRB_INT_PAR_OUTPUTFLAG, 0);
    if (GRBstartenv(env) != 0) {
      throw std::runtime_error("Gurobi environment start failed");
    }
    if (GRBnewmodel(env, &model, "bpp-rmp", 0, nullptr, nullptr, nullptr, nullptr, nullptr) != 0) {
      throw std::runtime_error("Gurobi RMP creation failed");
    }
    GRBsetintattr(model, GRB_INT_ATTR_MODELSENSE, GRB_MINIMIZE);
    // Same historical justification as CplexRmp (BPPS_BP_MASTER.cpp:1382-1383,
    // PARAM_SIMPLEX=1/PARAM_CPU=1): primal simplex, single thread, is the
    // best fit for a warm-started re-solve after adding a handful of
    // columns per column-generation iteration.
    GRBsetintparam(env, GRB_INT_PAR_METHOD, GRB_METHOD_PRIMAL);
    GRBsetintparam(env, GRB_INT_PAR_THREADS, 1);
    const std::size_t row_count = value.item_count() + cuts.size();
    for (std::size_t row = 0; row < row_count; ++row) {
      const char sense = row < value.item_count() ? GRB_GREATER_EQUAL : GRB_LESS_EQUAL;
      if (GRBaddconstr(model, 0, nullptr, nullptr, sense, 1.0, nullptr) != 0) {
        throw std::runtime_error("Gurobi RMP row creation failed");
      }
    }
    if (GRBupdatemodel(model) != 0) throw std::runtime_error("Gurobi RMP row commit failed");
  }

  ~Impl() {
    if (model != nullptr) GRBfreemodel(model);
    if (env != nullptr) GRBfreeenv(env);
  }
};

GurobiRmp::GurobiRmp(const Instance& instance, std::vector<Sr3Cut> cuts)
    : impl_(std::make_unique<Impl>(instance, std::move(cuts))) {}
GurobiRmp::~GurobiRmp() = default;
GurobiRmp::GurobiRmp(GurobiRmp&&) noexcept = default;
GurobiRmp& GurobiRmp::operator=(GurobiRmp&&) noexcept = default;

void GurobiRmp::add_pattern(const Pattern& pattern) {
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
  if (GRBaddvar(impl_->model, static_cast<int>(indices.size()), indices.data(), values.data(),
               1.0, 0.0, GRB_INFINITY, GRB_CONTINUOUS, nullptr) != 0) {
    throw std::runtime_error("Gurobi pattern column creation failed");
  }
  if (GRBupdatemodel(impl_->model) != 0) {
    throw std::runtime_error("Gurobi pattern column commit failed");
  }
  ++impl_->patterns;
  impl_->primals.resize(impl_->patterns, 0.0);
}

void GurobiRmp::add_cut(const Sr3Cut& cut, const std::vector<Pattern>& patterns) {
  if (patterns.size() != impl_->patterns) {
    throw std::invalid_argument("add_cut requires every existing column, in insertion order");
  }
  if (GRBaddconstr(impl_->model, 0, nullptr, nullptr, GRB_LESS_EQUAL, 1.0, nullptr) != 0) {
    throw std::runtime_error("Gurobi SR3 row creation failed");
  }
  if (GRBupdatemodel(impl_->model) != 0) {
    throw std::runtime_error("Gurobi SR3 row commit failed");
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
      GRBchgcoeffs(impl_->model, static_cast<int>(rows.size()), rows.data(), cols.data(),
                  values.data()) != 0) {
    throw std::runtime_error("Gurobi SR3 row coefficient assignment failed");
  }
  if (GRBupdatemodel(impl_->model) != 0) {
    throw std::runtime_error("Gurobi SR3 row coefficient commit failed");
  }
  impl_->cuts.push_back(cut);
  impl_->sr3_duals.push_back(0.0);
}

void GurobiRmp::solve() {
  if (impl_->patterns == 0) throw std::logic_error("cannot solve an RMP without patterns");
  if (GRBoptimize(impl_->model) != 0) throw std::runtime_error("Gurobi RMP solve failed");
  int status = 0;
  if (GRBgetintattr(impl_->model, GRB_INT_ATTR_STATUS, &status) != 0 || status != GRB_OPTIMAL) {
    throw std::runtime_error("Gurobi RMP did not reach optimality (status " +
                             std::to_string(status) + ")");
  }
  std::vector<double> all_duals(impl_->duals.size() + impl_->sr3_duals.size(), 0.0);
  if (GRBgetdblattr(impl_->model, GRB_DBL_ATTR_OBJVAL, &impl_->objective) != 0 ||
      GRBgetdblattrarray(impl_->model, GRB_DBL_ATTR_PI, 0, static_cast<int>(all_duals.size()),
                         all_duals.data()) != 0 ||
      GRBgetdblattrarray(impl_->model, GRB_DBL_ATTR_X, 0, static_cast<int>(impl_->primals.size()),
                         impl_->primals.data()) != 0) {
    throw std::runtime_error("Gurobi RMP solution extraction failed");
  }
  std::copy(all_duals.begin(), all_duals.begin() + static_cast<std::ptrdiff_t>(impl_->duals.size()),
            impl_->duals.begin());
  std::copy(all_duals.begin() + static_cast<std::ptrdiff_t>(impl_->duals.size()), all_duals.end(),
            impl_->sr3_duals.begin());
}

double GurobiRmp::objective_value() const noexcept { return impl_->objective; }
const std::vector<double>& GurobiRmp::duals() const noexcept { return impl_->duals; }
const std::vector<double>& GurobiRmp::sr3_duals() const noexcept { return impl_->sr3_duals; }
const std::vector<Sr3Cut>& GurobiRmp::sr3_cuts() const noexcept { return impl_->cuts; }
const std::vector<double>& GurobiRmp::primal_values() const noexcept { return impl_->primals; }
std::size_t GurobiRmp::pattern_count() const noexcept { return impl_->patterns; }

}  // namespace bpp

#else

namespace bpp {
struct GurobiRmp::Impl {};
GurobiRmp::GurobiRmp(const Instance&, std::vector<Sr3Cut>) { throw std::runtime_error("BPP was built without Gurobi"); }
GurobiRmp::~GurobiRmp() = default;
GurobiRmp::GurobiRmp(GurobiRmp&&) noexcept = default;
GurobiRmp& GurobiRmp::operator=(GurobiRmp&&) noexcept = default;
void GurobiRmp::add_pattern(const Pattern&) { throw std::runtime_error("BPP was built without Gurobi"); }
void GurobiRmp::add_cut(const Sr3Cut&, const std::vector<Pattern>&) { throw std::runtime_error("BPP was built without Gurobi"); }
void GurobiRmp::solve() { throw std::runtime_error("BPP was built without Gurobi"); }
double GurobiRmp::objective_value() const noexcept { return 0.0; }
const std::vector<double>& GurobiRmp::duals() const noexcept { static const std::vector<double> empty; return empty; }
const std::vector<double>& GurobiRmp::sr3_duals() const noexcept { static const std::vector<double> empty; return empty; }
const std::vector<Sr3Cut>& GurobiRmp::sr3_cuts() const noexcept { static const std::vector<Sr3Cut> empty; return empty; }
const std::vector<double>& GurobiRmp::primal_values() const noexcept { static const std::vector<double> empty; return empty; }
std::size_t GurobiRmp::pattern_count() const noexcept { return 0; }
}  // namespace bpp

#endif
