#pragma once

#include "bpp/cuts.hpp"
#include "bpp/instance.hpp"
#include "bpp/pattern.hpp"

#include <memory>
#include <vector>

namespace bpp {

// Rational safe restricted master. SoPlex headers and rationals remain
// private to the implementation; this adapter exposes only BPP vectors.
class SoplexRmp {
 public:
  explicit SoplexRmp(const Instance& instance, std::vector<Sr3Cut> cuts = {});
  ~SoplexRmp();
  SoplexRmp(SoplexRmp&&) noexcept;
  SoplexRmp& operator=(SoplexRmp&&) noexcept;
  SoplexRmp(const SoplexRmp&) = delete;
  SoplexRmp& operator=(const SoplexRmp&) = delete;

  void add_pattern(const Pattern& pattern);
  void solve();
  double objective_value() const noexcept;
  const std::vector<double>& duals() const noexcept;
  const std::vector<double>& sr3_duals() const noexcept;
  const std::vector<double>& primal_values() const noexcept;
  std::size_t pattern_count() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bpp
