#include "bpp/safe_bound.hpp"

#include <limits>
#include <cmath>
#include <stdexcept>
#include <utility>

#ifdef BPP_HAS_GMP
#include <gmpxx.h>
#endif

namespace bpp {

struct SafeBound::Impl {
#ifdef BPP_HAS_GMP
  mpq_class value;
  Impl(std::int64_t numerator, std::int64_t denominator) : value(numerator, denominator) {}
#else
  Impl(std::int64_t, std::int64_t) {}
#endif
};

SafeBound::SafeBound(std::int64_t numerator, std::int64_t denominator)
    : impl_(std::make_unique<Impl>(numerator, denominator)) {
  if (denominator <= 0) throw std::invalid_argument("safe-bound denominator must be positive");
#ifndef BPP_HAS_GMP
  throw std::runtime_error("safe bounds require GMP (configure with BPP_ENABLE_GMP=ON)");
#endif
}
SafeBound::~SafeBound() = default;
SafeBound::SafeBound(SafeBound&&) noexcept = default;
SafeBound& SafeBound::operator=(SafeBound&&) noexcept = default;
SafeBound::SafeBound(const SafeBound& other) : impl_(std::make_unique<Impl>(*other.impl_)) {}
SafeBound& SafeBound::operator=(const SafeBound& other) {
  if (this != &other) impl_ = std::make_unique<Impl>(*other.impl_);
  return *this;
}

SafeBound SafeBound::from_scaled_duals(const std::vector<std::int64_t>& duals,
                                       std::int64_t scale) {
  if (scale <= 0) throw std::invalid_argument("safe-bound scale must be positive");
#ifdef BPP_HAS_GMP
  SafeBound result(0, 1);
  for (const auto dual : duals) result.impl_->value += mpq_class(dual, scale);
  return result;
#else
  (void)duals;
  return SafeBound(0, scale);
#endif
}

SafeBound SafeBound::from_floating_duals(const std::vector<double>& duals,
                                         std::int64_t scale) {
  if (scale <= 0) throw std::invalid_argument("safe-bound scale must be positive");
  std::vector<std::int64_t> rounded;
  rounded.reserve(duals.size());
  for (const double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("floating duals must be finite");
    const long double scaled = std::nextafter(static_cast<long double>(dual),
                                               -std::numeric_limits<long double>::infinity()) * scale;
    const long double floored = std::floor(scaled);
    if (floored < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        floored > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
      throw std::overflow_error("scaled floating dual does not fit in int64");
    }
    rounded.push_back(static_cast<std::int64_t>(floored));
  }
  return from_scaled_duals(rounded, scale);
}

std::string SafeBound::str() const {
#ifdef BPP_HAS_GMP
  return impl_->value.get_str();
#else
  throw std::runtime_error("safe bounds require GMP");
#endif
}

std::int64_t SafeBound::ceil_bins() const {
#ifdef BPP_HAS_GMP
  const mpz_class numerator = impl_->value.get_num();
  const mpz_class denominator = impl_->value.get_den();
  mpz_class quotient = numerator / denominator;
  if (numerator > 0 && numerator % denominator != 0) ++quotient;
  if (!quotient.fits_sint_p()) throw std::overflow_error("safe bound does not fit in int64");
  return quotient.get_si();
#else
  throw std::runtime_error("safe bounds require GMP");
#endif
}

bool SafeBound::operator<(const SafeBound& other) const {
#ifdef BPP_HAS_GMP
  return impl_->value < other.impl_->value;
#else
  (void)other;
  throw std::runtime_error("safe bounds require GMP");
#endif
}

SafeBound SafeBound::fathoming_bound(std::int64_t rc_int, std::int64_t scale) const {
  if (scale <= 0) throw std::invalid_argument("fathoming-bound scale must be positive");
  if (scale - rc_int <= 0) {
    throw std::invalid_argument("fathoming bound requires scale - rc_int to be positive");
  }
#ifdef BPP_HAS_GMP
  SafeBound result(*this);
  result.impl_->value *= mpq_class(scale, 1);
  result.impl_->value /= mpq_class(scale - rc_int, 1);
  result.impl_->value.canonicalize();
  return result;
#else
  (void)rc_int;
  throw std::runtime_error("safe bounds require GMP");
#endif
}

std::optional<SafeBound> certify_scaled_duals(const std::vector<Pattern>& patterns,
                                              const std::vector<double>& duals,
                                              std::int64_t scale) {
  if (scale <= 0) throw std::invalid_argument("safe-bound scale must be positive");
  if (duals.empty() && !patterns.empty()) return std::nullopt;
  std::vector<std::int64_t> scaled;
  scaled.reserve(duals.size());
  for (const double dual : duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("floating duals must be finite");
    const long double value = std::floor(std::nextafter(static_cast<long double>(dual),
                                                         -std::numeric_limits<long double>::infinity()) * scale);
    if (value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
      throw std::overflow_error("scaled dual does not fit in int64");
    }
    scaled.push_back(static_cast<std::int64_t>(value));
  }
  for (const auto& pattern : patterns) {
    std::int64_t activity = 0;
    for (const int item : pattern.items()) {
      if (item < 0 || static_cast<std::size_t>(item) >= scaled.size()) {
        throw std::invalid_argument("safe-bound dual vector does not cover pattern items");
      }
      const auto value = scaled[static_cast<std::size_t>(item)];
      if ((value > 0 && activity > std::numeric_limits<std::int64_t>::max() - value) ||
          (value < 0 && activity < std::numeric_limits<std::int64_t>::min() - value)) {
        throw std::overflow_error("scaled pattern activity overflow");
      }
      activity += value;
    }
    if (activity > scale) return std::nullopt;
  }
  return SafeBound::from_scaled_duals(scaled, scale);
}

std::optional<SafeBound> certify_scaled_duals(const std::vector<Pattern>& patterns,
                                              const std::vector<double>& item_duals,
                                              const std::vector<Sr3Cut>& cuts,
                                              const std::vector<double>& cut_duals,
                                              std::int64_t scale) {
  if (scale <= 0) throw std::invalid_argument("safe-bound scale must be positive");
  if (cuts.size() != cut_duals.size()) {
    throw std::invalid_argument("one dual is required for every SR3 cut");
  }
  std::vector<std::int64_t> scaled_items;
  scaled_items.reserve(item_duals.size());
  for (double dual : item_duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("floating duals must be finite");
    const long double value = std::floor(std::nextafter(static_cast<long double>(dual),
                                                         -std::numeric_limits<long double>::infinity()) * scale);
    if (value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
      throw std::overflow_error("scaled dual does not fit in int64");
    }
    scaled_items.push_back(static_cast<std::int64_t>(value));
  }
  std::vector<std::int64_t> scaled_cuts;
  scaled_cuts.reserve(cut_duals.size());
  for (double dual : cut_duals) {
    if (!std::isfinite(dual)) throw std::invalid_argument("floating cut duals must be finite");
    const long double value = std::floor(std::nextafter(static_cast<long double>(dual),
                                                         -std::numeric_limits<long double>::infinity()) * scale);
    if (value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
      throw std::overflow_error("scaled cut dual does not fit in int64");
    }
    scaled_cuts.push_back(static_cast<std::int64_t>(value));
  }
  for (const auto& pattern : patterns) {
    std::int64_t activity = 0;
    for (int item : pattern.items()) {
      if (item < 0 || static_cast<std::size_t>(item) >= scaled_items.size()) {
        throw std::invalid_argument("safe-bound dual vector does not cover pattern items");
      }
      const auto value = scaled_items[static_cast<std::size_t>(item)];
      if ((value > 0 && activity > std::numeric_limits<std::int64_t>::max() - value) ||
          (value < 0 && activity < std::numeric_limits<std::int64_t>::min() - value)) {
        throw std::overflow_error("scaled pattern activity overflow");
      }
      activity += value;
    }
    for (std::size_t cut = 0; cut < cuts.size(); ++cut) {
      const auto coefficient = cuts[cut].coefficient(pattern);
      const auto value = scaled_cuts[cut] * static_cast<std::int64_t>(coefficient);
      if ((value > 0 && activity > std::numeric_limits<std::int64_t>::max() - value) ||
          (value < 0 && activity < std::numeric_limits<std::int64_t>::min() - value)) {
        throw std::overflow_error("scaled SR3 activity overflow");
      }
      activity += value;
    }
    if (activity > scale) return std::nullopt;
  }
  scaled_items.insert(scaled_items.end(), scaled_cuts.begin(), scaled_cuts.end());
  return SafeBound::from_scaled_duals(scaled_items, scale);
}

}  // namespace bpp
