#include "bpp/soplex_rmp.hpp"

#include <stdexcept>
#include <utility>

#ifdef BPP_HAS_SOPLEX
#include <soplex.h>

namespace bpp {

struct SoplexRmp::Impl {
  const Instance* instance;
  std::vector<Sr3Cut> cuts;
  soplex::SoPlex solver;
  std::vector<double> duals;
  std::vector<double> sr3_duals;
  std::vector<double> primals;
  double objective = 0.0;
  std::size_t patterns = 0;

  explicit Impl(const Instance& value, std::vector<Sr3Cut> active_cuts)
      : instance(&value), cuts(std::move(active_cuts)), duals(value.item_count(), 0.0),
        sr3_duals(cuts.size(), 0.0) {
    solver.setIntParam(soplex::SoPlex::READMODE, soplex::SoPlex::READMODE_RATIONAL);
    solver.setIntParam(soplex::SoPlex::SOLVEMODE, soplex::SoPlex::SOLVEMODE_RATIONAL);
    solver.setIntParam(soplex::SoPlex::CHECKMODE, soplex::SoPlex::CHECKMODE_RATIONAL);
    solver.setIntParam(soplex::SoPlex::SYNCMODE, soplex::SoPlex::SYNCMODE_AUTO);
    solver.setIntParam(soplex::SoPlex::VERBOSITY, 0);
    solver.setIntParam(soplex::SoPlex::OBJSENSE, soplex::SoPlex::OBJSENSE_MINIMIZE);
    solver.setRealParam(soplex::SoPlex::FEASTOL, 0.0);
    solver.setRealParam(soplex::SoPlex::OPTTOL, 0.0);
    for (std::size_t item = 0; item < value.item_count(); ++item) {
      soplex::DSVectorRational row;
      solver.addRowRational(soplex::LPRowRational(soplex::Rational(1), row,
                                                  soplex::Rational(soplex::infinity)));
    }
    for (std::size_t cut = 0; cut < cuts.size(); ++cut) {
      soplex::DSVectorRational row;
      solver.addRowRational(soplex::LPRowRational(soplex::Rational(-soplex::infinity), row,
                                                  soplex::Rational(1)));
    }
  }
};

SoplexRmp::SoplexRmp(const Instance& instance, std::vector<Sr3Cut> cuts)
    : impl_(std::make_unique<Impl>(instance, std::move(cuts))) {}
SoplexRmp::~SoplexRmp() = default;
SoplexRmp::SoplexRmp(SoplexRmp&&) noexcept = default;
SoplexRmp& SoplexRmp::operator=(SoplexRmp&&) noexcept = default;

void SoplexRmp::add_pattern(const Pattern& pattern) {
  soplex::DSVectorRational column;
  for (int item : pattern.items()) column.add(item, soplex::Rational(1));
  for (std::size_t cut = 0; cut < impl_->cuts.size(); ++cut) {
    const int coefficient = impl_->cuts[cut].coefficient(pattern);
    if (coefficient != 0) {
      column.add(static_cast<int>(impl_->instance->item_count() + cut),
                 soplex::Rational(coefficient));
    }
  }
  impl_->solver.addColRational(soplex::LPColRational(soplex::Rational(1), column,
                                                    soplex::Rational(soplex::infinity), soplex::Rational(0)));
  ++impl_->patterns;
  impl_->primals.resize(impl_->patterns, 0.0);
}

void SoplexRmp::solve() {
  if (impl_->patterns == 0) throw std::logic_error("cannot solve a SoPlex RMP without patterns");
  const auto status = impl_->solver.optimize();
  if (status != soplex::SPxSolver::OPTIMAL) {
    throw std::runtime_error("SoPlex RMP did not reach optimality (status " +
                             std::to_string(static_cast<int>(status)) + ")");
  }
  const auto objective = impl_->solver.objValueRational();
  impl_->objective = static_cast<double>(objective);
  soplex::DVectorRational dual(impl_->duals.size() + impl_->sr3_duals.size());
  soplex::DVectorRational primal(impl_->primals.size());
  if (!impl_->solver.getDualRational(dual) || !impl_->solver.getPrimalRational(primal)) {
    throw std::runtime_error("SoPlex rational solution extraction failed");
  }
  for (std::size_t item = 0; item < impl_->duals.size(); ++item) impl_->duals[item] = static_cast<double>(dual[item]);
  for (std::size_t cut = 0; cut < impl_->sr3_duals.size(); ++cut) {
    impl_->sr3_duals[cut] = static_cast<double>(dual[impl_->duals.size() + cut]);
  }
  for (std::size_t column = 0; column < impl_->primals.size(); ++column) {
    impl_->primals[column] = static_cast<double>(primal[column]);
  }
}

double SoplexRmp::objective_value() const noexcept { return impl_->objective; }
const std::vector<double>& SoplexRmp::duals() const noexcept { return impl_->duals; }
const std::vector<double>& SoplexRmp::sr3_duals() const noexcept { return impl_->sr3_duals; }
const std::vector<double>& SoplexRmp::primal_values() const noexcept { return impl_->primals; }
std::size_t SoplexRmp::pattern_count() const noexcept { return impl_->patterns; }

}  // namespace bpp

#else

namespace bpp {
struct SoplexRmp::Impl {};
SoplexRmp::SoplexRmp(const Instance&, std::vector<Sr3Cut>) {
  throw std::runtime_error("BPP was built without SoPlex");
}
SoplexRmp::~SoplexRmp() = default;
SoplexRmp::SoplexRmp(SoplexRmp&&) noexcept = default;
SoplexRmp& SoplexRmp::operator=(SoplexRmp&&) noexcept = default;
void SoplexRmp::add_pattern(const Pattern&) { throw std::runtime_error("BPP was built without SoPlex"); }
void SoplexRmp::solve() { throw std::runtime_error("BPP was built without SoPlex"); }
double SoplexRmp::objective_value() const noexcept { return 0.0; }
const std::vector<double>& SoplexRmp::duals() const noexcept { static const std::vector<double> empty; return empty; }
const std::vector<double>& SoplexRmp::sr3_duals() const noexcept { static const std::vector<double> empty; return empty; }
const std::vector<double>& SoplexRmp::primal_values() const noexcept { static const std::vector<double> empty; return empty; }
std::size_t SoplexRmp::pattern_count() const noexcept { return 0; }
}  // namespace bpp

#endif
