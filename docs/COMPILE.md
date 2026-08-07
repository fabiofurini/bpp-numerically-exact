# Compile

[← Back to README](../README.md) · [← Code](CODE.md)

## Dependencies

| Dependency | Required? | Purpose |
|---|---|---|
| CMake ≥ 3.16, a C++17 compiler | Always | Build system |
| IBM ILOG CPLEX **or** Gurobi | For the exact solver | Floating-point LP phase (either is sufficient alone) |
| SoPlex (built with GMP support) | For the exact solver | Rational LP certification phase |
| GMP | For the exact solver | Exact rational-number arithmetic |

None of CPLEX, Gurobi, or SoPlex is redistributed in this repository —
each must be installed/built separately. Without any of them, the
portable build still produces a working `bpp-solve` binary that runs the
greedy fallback heuristic (useful for testing the build itself, not for
numerically exact solving).

## Portable build (no external solver)

```sh
cmake -S . -B build -DBPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Produces `build/bpp-solve` plus the three test binaries. This is enough
to validate the instance model, parser, and solution checker, and to run
the greedy fallback.

## Full numerically exact build (CPLEX)

```sh
cmake -S . -B build-cplex-soplex -DBPP_BUILD_TESTS=ON \
  -DBPP_ENABLE_CPLEX=ON -DBPP_ENABLE_SOPLEX=ON -DBPP_ENABLE_GMP=ON \
  -DCPLEX_ROOT=/path/to/CPLEX_Studio \
  -DSOPLEX_BUILD_ROOT=/path/to/soplex-build-large
cmake --build build-cplex-soplex -j2
ctest --test-dir build-cplex-soplex --output-on-failure
```

## Full numerically exact build (Gurobi, alternative to CPLEX)

```sh
cmake -S . -B build-gurobi-soplex -DBPP_BUILD_TESTS=ON \
  -DBPP_ENABLE_GUROBI=ON -DBPP_ENABLE_SOPLEX=ON -DBPP_ENABLE_GMP=ON \
  -DGUROBI_ROOT=/path/to/gurobiNNN/linux64 \
  -DSOPLEX_BUILD_ROOT=/path/to/soplex-build-large
cmake --build build-gurobi-soplex -j2
ctest --test-dir build-gurobi-soplex --output-on-failure
```

Either `-DBPP_ENABLE_CPLEX=ON` or `-DBPP_ENABLE_GUROBI=ON` alone produces
a complete, fully supported build. Both together
(`-DBPP_ENABLE_CPLEX=ON -DBPP_ENABLE_GUROBI=ON`) additionally enables a
CPLEX-vs-Gurobi equivalence test and lets you pick the backend at runtime
with `--solver cplex|gurobi`. SoPlex and GMP are mandatory in every exact
configuration — CMake refuses a CPLEX- or Gurobi-only build without
SoPlex, since the rational certification phase is what makes results
*numerically exact* rather than a floating-point heuristic bound.

A CPLEX build also produces `bpp-solve-legacy`, a build of the historical
comparison implementation used during development as a regression oracle
(kept local-only; not part of this repository's own source, and not
redistributed — see `NOTICE`).

## Running the tests

```sh
ctest --test-dir <build-dir> --output-on-failure
```

Runs three CTest targets:

- **bpp-unit-tests** — isolated components, always built and runnable
  regardless of which backends are enabled.
- **bpp-integration-tests** — multi-component `solve_*` pipelines;
  most of these require a CPLEX- and/or Gurobi-enabled build to exercise
  anything beyond the portable fallback.
- **bpp-regression-tests** — pinned to specific, previously-fixed bugs.

See each file under `tests/` for the exact scope of what it covers.

---

Next: [Usage — how to call the solver](USAGE.md)
