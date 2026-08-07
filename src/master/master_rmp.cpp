#include "bpp/master_rmp.hpp"

#include <utility>

namespace bpp {

namespace {
std::variant<CplexRmp, GurobiRmp> make_backend(LpBackend backend, const Instance& instance,
                                               std::vector<Sr3Cut> cuts) {
  if (backend == LpBackend::Gurobi) return std::variant<CplexRmp, GurobiRmp>(std::in_place_type<GurobiRmp>, instance, std::move(cuts));
  return std::variant<CplexRmp, GurobiRmp>(std::in_place_type<CplexRmp>, instance, std::move(cuts));
}
}  // namespace

MasterRmp::MasterRmp(LpBackend backend, const Instance& instance, std::vector<Sr3Cut> cuts)
    : backend_(make_backend(backend, instance, std::move(cuts))) {}

void MasterRmp::add_pattern(const Pattern& pattern) {
  std::visit([&](auto& rmp) { rmp.add_pattern(pattern); }, backend_);
}

void MasterRmp::add_cut(const Sr3Cut& cut, const std::vector<Pattern>& patterns) {
  std::visit([&](auto& rmp) { rmp.add_cut(cut, patterns); }, backend_);
}

void MasterRmp::solve() {
  std::visit([](auto& rmp) { rmp.solve(); }, backend_);
}

double MasterRmp::objective_value() const noexcept {
  return std::visit([](const auto& rmp) { return rmp.objective_value(); }, backend_);
}

const std::vector<double>& MasterRmp::duals() const noexcept {
  return std::visit([](const auto& rmp) -> const std::vector<double>& { return rmp.duals(); }, backend_);
}

const std::vector<double>& MasterRmp::sr3_duals() const noexcept {
  return std::visit([](const auto& rmp) -> const std::vector<double>& { return rmp.sr3_duals(); }, backend_);
}

const std::vector<Sr3Cut>& MasterRmp::sr3_cuts() const noexcept {
  return std::visit([](const auto& rmp) -> const std::vector<Sr3Cut>& { return rmp.sr3_cuts(); }, backend_);
}

const std::vector<double>& MasterRmp::primal_values() const noexcept {
  return std::visit([](const auto& rmp) -> const std::vector<double>& { return rmp.primal_values(); }, backend_);
}

std::size_t MasterRmp::pattern_count() const noexcept {
  return std::visit([](const auto& rmp) { return rmp.pattern_count(); }, backend_);
}

}  // namespace bpp
