# Code

[← Back to README](../README.md)

## Layout

```
include/bpp/     Public headers (one class/module per file)
src/
  core/          Instance, Pattern, Solution: validated domain model
  io/            Instance reader, solution writer
  cuts/          SR3 (triplet) cut separation
  pricing/       Pricing oracles: label-setting DPs (root, SR3-aware,
                 branch-aware), scaled-integer variants for Algorithm 1
  master/        Restricted master problem backends: CplexRmp, GurobiRmp,
                 SoplexRmp (rational), MasterRmp (backend-agnostic wrapper),
                 column_generation.cpp (the Algorithm 1 driver),
                 safe_bound.cpp (exact GMP rational certification)
  search/        Ryan–Foster branch-and-price tree (best-bound and
                 depth-first strategies), diving heuristic
  solver/        Greedy fallback heuristics (best-fit-decreasing, etc.)
  cli/           bpp-solve command-line entry point
tests/           bpp-unit-tests / bpp-integration-tests / bpp-regression-tests
docs/            Documentation (this file, compile, usage, input/output
                 reference, engineering status and design notes)
```

## What implements what

| Paper concept | Where |
|---|---|
| Algorithm 1 (scaled-integer-dual two-phase column generation) | `src/master/column_generation.cpp` |
| Pricing oracles (root, SR3-aware, branch-aware label-setting DPs) | `src/pricing/floating_root_pricer.cpp` |
| SR3 (triplet) cut separation | `src/cuts/sr3.cpp` |
| Exact rational certification (GMP) | `src/master/safe_bound.cpp` |
| Section 4 MIP-based exact certification | `src/master/cplex_rmp.cpp` (`solve_mip_at_most`), driven from `column_generation.cpp` |
| Ryan–Foster branch-and-price tree | `src/search/branch_and_price.cpp` |
| Floating-point LP backends (CPLEX / Gurobi) | `src/master/cplex_rmp.cpp`, `src/master/gurobi_rmp.cpp`, unified by `src/master/master_rmp.cpp` |
| Rational LP backend (SoPlex) | `src/master/soplex_rmp.cpp` |

---

Next: [Compile](COMPILE.md)
