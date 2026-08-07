## Summary

What does this change, and why?

## Numerical-safety checklist (required for anything touching pricing, bounds, or pruning)

- [ ] This change does not let a floating-point value alone decide a prune/fathom.
- [ ] I documented whether this affects the floating CPLEX phase, the rational
      SoPlex/GMP phase, or both (CONTRIBUTING.md).
- [ ] `ctest` passes locally on the portable build (`build/`).
- [ ] `ctest` passes locally on the CPLEX+SoPlex build (`build-cplex-soplex/`), if available to you.
- [ ] Added/updated a regression test if this changes parsing, pricing, bounds,
      branching, or pruning behavior.

## If this is an algorithmic/behavioral change

- [ ] I opened an issue first (per CONTRIBUTING.md) and linked it here: #
- [ ] I checked whether this affects a historical-parity claim in `docs/STATUS.md`
      and updated it with measured (not estimated) numbers if so.
