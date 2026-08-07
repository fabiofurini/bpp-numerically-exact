# Legacy feature inventory

This document is the migration contract for the classical BPP path in the
historical source tree at `../ARCHIVIO_CODICE/SETUP_BPP_CODE/`. The requirement
is functional equivalence: no BPP feature listed here may be silently dropped,
approximated or replaced during the refactor.

## Entry points and configuration

- Main solver: `src/Main.cpp`, with the historical positional arguments used
  by the classical BPP executable. The separate bilevel/strong-branching
  switch belongs to the BPPS path and is not part of this BPP target.
- Batch/file generation: `main_FILE_GENERATION` in `src/Main.cpp`.
- Ancillary local-search executable: `source/main.cpp`.
- The parameter reader and all legacy parameters must be represented by a
  typed configuration object. The public CLI may use named options, but must
  provide a compatibility mode for historical parameter files.

## BPP scope and excluded variants

- Classical bin packing (`instance_read_BPP`).
- Preprocessing and BPP reductions (`preprocessing.*`, `REDUCTIONS.*`), with
  reversible mapping to original item identifiers.

Bin packing with conflicts, setup/classes, Formulation A, Formulation B and
the ancillary local-search program are outside the new project's scope. Their
presence in the archive must not leak into the BPP model or public API.

## Exact optimisation components

- Restricted master construction and column-and-row generation
  (`BPPS_BP_MASTER.*`).
- Rational/safe master and SoPlex/GMP numerical certification
  (`BPPS_BP_MASTER_SOPLEX.*`).
- Dynamic-programming pricing, population/enumeration pricing and their
  heuristic variants (`DP.*`, `DP_POP.*`, `DP_POP_HEUR.*`,
  `POPULATE_MASTERS.*`). The port must preserve the floating and safe
  fixed-point pricing paths, scaling, label limits, dominances and every
  fathoming rule used by the BPP path.
- SR3/triplet separation (`BPPS_BP_TRIPLETS.*`).
- Ryan--Foster branch-price-and-cut tree and node mappings
  (`BPPS_BP_TREE.*`, `BPPS_BP_MAPPING.*`), including same/different branches,
  super-items and the historical branching-selection behavior. BPPS-only
  bilevel/strong branching is excluded from this classical-BPP scope.
- Diving, LP heuristics and stabilisation (`BPPS_BP_DIVING.*`,
  `BPPS_BP_LP_HEUR.*`, `BPPS_BP_MAGIC_STAB.*`). These are required primal
  heuristics, not optional replacements for the exact search.

## Solver backends and numerical contract

- CPLEX is the primary MIP/LP backend and must remain behind an adapter, with
  no proprietary headers in the public API.
- SoPlex and GMP provide the safe/rational phase and cannot be removed while
  preserving the historical numerically-exact claim.
- Gurobi paths are excluded: they are not part of the CPLEX/SoPlex BPP
  algorithm being migrated.

## Migration order

1. Capture executable baseline cases for the BPP CPLEX/SoPlex path.
2. Introduce typed BPP models and readers, retaining stable original IDs and
   reconstruction mappings.
3. Port the CPLEX restricted master and floating pricing with output-equivalence
   tests.
4. Port SoPlex/GMP safe bounds, integer/fixed-point pricing and all pruning
   rules before declaring a result exact.
5. Port tree, SR3, enumeration, diving, stabilisation and all primal
   heuristics; compare bounds, solution, statuses and counters to the baseline.

The small `bpp-solve` greedy program is only a core smoke test. It is not a
substitute for the historical pricing, branching, diving or primal heuristics.
