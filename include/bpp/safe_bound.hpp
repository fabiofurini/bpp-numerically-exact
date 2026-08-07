#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bpp/pattern.hpp"
#include "bpp/cuts.hpp"

namespace bpp {

// Exact non-negative rational used for pruning and certification. The GMP
// representation is private so the public BPP API remains dependency-neutral.
class SafeBound {
 public:
  SafeBound(std::int64_t numerator = 0, std::int64_t denominator = 1);
  ~SafeBound();
  SafeBound(SafeBound&&) noexcept;
  SafeBound& operator=(SafeBound&&) noexcept;
  SafeBound(const SafeBound&);
  SafeBound& operator=(const SafeBound&);

  static SafeBound from_scaled_duals(const std::vector<std::int64_t>& duals,
                                     std::int64_t scale);
  static SafeBound from_floating_duals(const std::vector<double>& duals,
                                       std::int64_t scale);
  std::string str() const;
  std::int64_t ceil_bins() const;
  bool operator<(const SafeBound& other) const;

  // Algorithm 1's Farley-type fathoming bound (Baldacci et al. 2023,
  // Proposition 3): LBF = z_dim * scale / (scale - rc_int), where this
  // SafeBound holds z_dim (the raw diminished-dual sum, from
  // from_scaled_duals over the *same* scale) and `rc_int` is the scaled
  // integer reduced cost of the best pricer candidate (negative, since this
  // is only meaningful when a genuinely improving pattern was found).
  // Exact rational arithmetic throughout; requires scale - rc_int > 0.
  SafeBound fathoming_bound(std::int64_t rc_int, std::int64_t scale) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Floors floating duals to the requested integer scale, verifies dual
// feasibility on every current master column, and returns a certified bound
// only when no scaled reduced cost is negative.
std::optional<SafeBound> certify_scaled_duals(const std::vector<Pattern>& patterns,
                                              const std::vector<double>& duals,
                                              std::int64_t scale);

std::optional<SafeBound> certify_scaled_duals(const std::vector<Pattern>& patterns,
                                              const std::vector<double>& item_duals,
                                              const std::vector<Sr3Cut>& cuts,
                                              const std::vector<double>& cut_duals,
                                              std::int64_t scale);

}  // namespace bpp
