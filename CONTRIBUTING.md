# Contributing

The solver targets the classical one-dimensional BPP. Please open an issue before adding a new algorithmic feature. Every change must keep the core tests green and include a regression test when it changes parsing, pricing, bounds, branching or pruning.

Changes to numerical bounds must document whether they affect the floating-point CPLEX phase, the rational SoPlex/GMP certification phase, or both. No decision that can prune a node may depend on an uncertified floating-point value.
