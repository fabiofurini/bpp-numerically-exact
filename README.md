# Numerically Exact Bin Packing

A numerically exact branch-price-and-cut solver for the classical
one-dimensional Bin-Packing Problem, implementing the algorithm described
by Baldacci, Coniglio, Cordeau, Furini, *"A Numerically Exact Algorithm
for the Bin-Packing Problem"* (INFORMS Journal on Computing, 2023):
Algorithm 1's scaled-integer-dual two-phase column generation, SR3
cutting planes, Ryan–Foster branch-and-price, and the paper's Section 4
MIP-based exact certification step.

**Documentation:** [Build & Compile](docs/BUILD.md) ·
[Usage](docs/USAGE.md) · [Input format](docs/INPUT.md) ·
[Output format](docs/OUTPUT.md)

This is an independent, clean-room reimplementation, not the paper's own
code. The official code and data are published by the authors at
[stefanoconiglio/A-Numerically-Exact-Algorithm-for-the-Bin-Packing-Problem](https://github.com/stefanoconiglio/A-Numerically-Exact-Algorithm-for-the-Bin-Packing-Problem)
— see `CITATION.cff`.

The public numerically-exact solver requires SoPlex and GMP plus at least one
floating-point LP backend, IBM ILOG CPLEX and/or Gurobi (either alone is
enough; running the complete test suite needs both, see below). None of
CPLEX, Gurobi, or SoPlex is redistributed in this repository. The final
backend preserves the paper's two column-generation phases: a fast
CPLEX-or-Gurobi phase followed by mandatory rational SoPlex/GMP
certification.

Verified against the official reference implementation on all 50
instances of the paper's ANI-201 benchmark family: all 50 certify a
numerically exact integer optimum, at a mean running time within ~2.7x of
the official implementation (see `docs/STATUS.md` for the full
measurement and methodology).

## Build and smoke test

```sh
cmake -S . -B build -DBPP_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

This leaves the portable executable at `build/bpp-solve` and three test
binaries: `build/bpp-unit-tests` (isolated components, always built),
`build/bpp-integration-tests` (multi-component `solve_*` pipelines, requires
a CPLEX-enabled build to do anything) and `build/bpp-regression-tests`
(pinned to specific previously-fixed bugs; see each file under `tests/` for
its exact scope). `ctest` runs all three. Build directories are ignored by
Git and can be recreated at any time.

For the complete numerically-exact backend, configure a second local build
against the external CPLEX installation and compiled SoPlex tree:

```sh
cmake -S . -B build-cplex-soplex -DBPP_BUILD_TESTS=ON \
  -DBPP_ENABLE_CPLEX=ON -DBPP_ENABLE_SOPLEX=ON -DBPP_ENABLE_GMP=ON \
  -DCPLEX_ROOT=/path/to/CPLEX_Studio \
  -DSOPLEX_BUILD_ROOT=/path/to/soplex-build-large
cmake --build build-cplex-soplex -j2
ctest --test-dir build-cplex-soplex --output-on-failure
```

Gurobi is an alternative to CPLEX for the same floating-point phase (see
`--solver` below), enabled the same way:

```sh
cmake -S . -B build-gurobi-soplex -DBPP_BUILD_TESTS=ON \
  -DBPP_ENABLE_GUROBI=ON -DBPP_ENABLE_SOPLEX=ON -DBPP_ENABLE_GMP=ON \
  -DGUROBI_ROOT=/path/to/gurobiNNN/linux64 \
  -DSOPLEX_BUILD_ROOT=/path/to/soplex-build-large
cmake --build build-gurobi-soplex -j2
ctest --test-dir build-gurobi-soplex --output-on-failure
```

Either flag can be set alone (a fully supported, complete build) or both
together (`-DBPP_ENABLE_CPLEX=ON -DBPP_ENABLE_GUROBI=ON`, selecting between
them at runtime with `--solver`) -- the complete test suite's CPLEX-vs-Gurobi
equivalence check (`tests/test_integration.cpp`) only runs when both are
built in, but everything else works with just one. SoPlex/GMP are mandatory
for the paper's numerically safe certification either way; CMake rejects a
CPLEX-only or Gurobi-only build without SoPlex.

A CPLEX build additionally produces the historical comparison binary
`build-cplex-soplex/bpp-solve-legacy` (always CPLEX+SoPlex, matching the
historical executable's own fixed choice, regardless of `BPP_ENABLE_GUROBI`).

The instance file format is one line containing `<number_of_items> <capacity>`, followed by one integer weight per item, one per line.

With a CPLEX- and/or Gurobi-enabled build, the executable exposes the
migrated root and tree paths:

```sh
build-cplex-soplex/bpp-solve INSTANCE --root-cg [MAX_ITERATIONS]
build-cplex-soplex/bpp-solve INSTANCE --no-populate [MAX_ITERATIONS]
build-cplex-soplex/bpp-solve INSTANCE --populate [MAX_COLUMNS]
build-cplex-soplex/bpp-solve INSTANCE --branch-price [MAX_NODES] [--strategy best-bound|depth-first]
```

`--solver cplex|gurobi` selects the floating-point LP backend on any of the
modes above (legacy `PARAM_SOLVER`); it defaults to whichever backend the
binary was actually built with, CPLEX if both are built in. Requesting a
backend that was not compiled in fails with an explicit error rather than
silently falling back to the other.

Every mode also accepts `--sr3-gap-activation VALUE` and `--sr3-max-cuts VALUE`
to tighten the two historical SR3 activation gates (`PARAM_TRIPLET_GAP_ACT`
and `PARAM_MAX_TRIPLETS`; see `docs/STATUS.md`) from their permissive
defaults.

`--root-cg` runs the CPLEX floating phase and, after floating convergence, the
mandatory rational SoPlex safe phase when the build is configured with
`-DBPP_ENABLE_SOPLEX=ON`. A CPLEX-only fixed-point/GMP fallback remains only as
a source-level diagnostic path; CMake rejects a CPLEX build without SoPlex and
the fallback must never be presented as numerically exact.
`--no-populate` (also spelled `--legacy-root-cg`) is the historical
no-populate compatibility path and stops after the floating root phase. Both
commands report phase counters, the backend and `phase2_verified`. The third
`--populate MAX_COLUMNS` runs the safe root path and then the bounded historical
gap population; it reports whether the requested column cap was reached.

The fourth command, `--branch-price`, runs the Ryan--Foster branch-and-price
tree to a certified integer optimum (or the node limit) and reports the
incumbent plus processed/generated/pruned nodes and a certified
`lower_bound_safe`. `--strategy` selects the node exploration order:
`best-bound` (default) always solves the pending node with the smallest
certified bound next; `depth-first` matches the historical tree's traversal
order (always exploring the Together branch before Different, with
backtracking) and warm-starts each node's column generation from a pool of
every pattern priced anywhere in the tree so far, instead of starting every
node from scratch. Both are exact; they only trade off exploration order and
how much column-generation work is reused across nodes. See
`docs/STATUS.md` for measured performance under each strategy.

Two historical features are ported as explicit opt-in flags, off by default
so the default path stays exactly what the paper describes: `--diving`
(`--branch-price` only) runs the historical bounded diving heuristic once
after the main tree search; `--stabilization` (`--root-cg`/`--populate`/
`--branch-price`) enables static dual-value smoothing, only ever active at
the root with no SR3/branching (matching legacy's own gate), and confirmed
to have been switched off in every historical parameter file found,
including the ones used to produce the paper's reported results. See
`bpp-solve --help` for every flag or `docs/STATUS.md` for the legacy source
verification behind each one.

Automatic SR3/triplet separation is enabled for `--root-cg`, `--populate` and
`--branch-price` as a bounded column-and-row restart, using a label-setting
DP pricer that carries per-cut dual state (not an exhaustive search) so it
stays practical on real-sized instances. The compatibility `--no-populate` /
`--legacy-root-cg` mode remains cut-free. The historical activation schedule
and complete tree interaction still require validation; see
[`docs/STATUS.md`](docs/STATUS.md).

The complete documentation index is [`docs/README.md`](docs/README.md).
For continuation by another LLM, start with
[`docs/CONTINUATION_STATE.md`](docs/CONTINUATION_STATE.md).

## Provenance

This repository is an independent, clean-room reimplementation of the
algorithm described in the paper. The paper's own official code and data
are published separately by the authors at
<https://github.com/stefanoconiglio/A-Numerically-Exact-Algorithm-for-the-Bin-Packing-Problem>
-- see `CITATION.cff` to cite it alongside the paper itself.
