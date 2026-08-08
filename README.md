# Numerically Exact Bin Packing

A numerically exact branch-price-and-cut solver for the classical
one-dimensional Bin-Packing Problem, implementing Baldacci, Coniglio,
Cordeau, Furini, *"A Numerically Exact Algorithm for the Bin-Packing
Problem"* (INFORMS Journal on Computing, 2023). Independent, clean-room
reimplementation — see `CITATION.cff`.

## What the solver does

Given a set of items with integer weights and a bin capacity, the solver
finds the minimum number of bins that fit every item and — unlike a
floating-point MILP solve — **proves** that number is optimal with an
exact rational certificate, not just a numerical claim. The example below
is one certified-optimal 66-bin solution for a real 201-item benchmark
instance (`201_2500_NR_0`, capacity 2456): each column is one bin, height
is capacity, and each colored block is one item, labeled with its index
and sized to its weight.

![One certified-optimal solution: 66 bins, each column height equal to bin capacity, each colored block one item labeled by its index and sized to its weight](docs/images/solution-example.svg)

Most bins here pair one large item with one or two small "filler" items —
typical of the ANI benchmark family, whose difficulty comes from items
sized just under half the capacity (see [Input](docs/INPUT.md)). The last
two bins instead absorb many small leftover items each — a mix the solver
has to prove is optimal alongside the simpler pairs, not just find.

## How it works

The algorithm follows the paper's two-phase column-generation scheme,
with each stage opt-in only where it goes beyond the paper's own method:

1. **Column generation (Algorithm 1)** — floating-point phase (CPLEX or
   Gurobi) followed by a rational phase (SoPlex/GMP) that re-certifies
   the same bound in infinite precision, closing the gap a purely
   floating-point solver cannot: the two phases share one persistent
   master, so nothing computed in phase I is discarded.
2. **Pricing** — a dynamic-programming label-setting algorithm finds
   negative-reduced-cost patterns (bins) exactly, in scaled-integer
   arithmetic, rather than approximating with a heuristic.
3. **SR3 (triplet) cut separation** — automatically strengthens the LP
   relaxation with subset-row cuts on triples of items, run to a bounded
   number of simultaneous cuts so the pricing DP stays fast (see
   `docs/STATUS.md` for the exact dominance mechanism this relies on).
4. **Root-node exact certification (Section 4)** — when the LP+cuts bound
   alone doesn't close the integrality gap, a genuine 0/1 covering MIP is
   solved on the enumerated column pool; infeasibility of "beat the
   incumbent by 1" is itself a complete proof of optimality, independent
   of the LP bound.
5. **Ryan-Foster branch-and-price** — the fallback for the (rare) instances
   the root alone cannot certify: a full enumeration tree with the same
   exact pricing and cut machinery at every node.

Every one of these stages produces or preserves an **exact rational
bound** (via GMP), so the final answer is never "probably optimal" — it's
optimal, with a certificate. See [Code](docs/CODE.md) for how each stage
maps onto the source, and `docs/STATUS.md`/`docs/ROADMAP.md` for the
project's current state and what's still open.

## Documentation

1. [Code](docs/CODE.md)
2. [Compile](docs/COMPILE.md)
3. [Usage](docs/USAGE.md)
4. [Input](docs/INPUT.md)
5. [Output](docs/OUTPUT.md)

[Full documentation index](docs/README.md)

## License

MIT (see `LICENSE`), crediting all four paper co-authors.
