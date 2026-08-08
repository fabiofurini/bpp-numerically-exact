# Numerically Exact Bin Packing

A numerically exact branch-price-and-cut solver for the classical
one-dimensional Bin-Packing Problem, implementing Baldacci, Coniglio,
Cordeau, Furini, *"A Numerically Exact Algorithm for the Bin-Packing
Problem"* (INFORMS Journal on Computing, 2023). It was reconstructed from the
paper with the historical C++ source used as an implementation reference and
validation aid — see `CITATION.cff`. The paper's own official reference executable is at
[stefanoconiglio/A-Numerically-Exact-Algorithm-for-the-Bin-Packing-Problem](https://github.com/stefanoconiglio/A-Numerically-Exact-Algorithm-for-the-Bin-Packing-Problem).

## What the solver does

Given a set of items with integer weights and a bin capacity, the solver
finds the minimum number of bins that fit every item and — unlike a
floating-point MILP solve — **proves** that number is optimal with an
exact rational certificate, not just a numerical claim. See
[a worked example](docs/EXAMPLE.md) for a figure of one certified-optimal
solution, bin by bin, with a reading guide.

## How it works

1. **Column generation** — a floating-point phase (CPLEX or Gurobi) finds
   a near-optimal set of bins quickly, then a rational phase (SoPlex/GMP)
   re-certifies the same bound in infinite precision, closing the gap a
   purely floating-point solver cannot. The rational master is rebuilt in
   memory from the current columns and every active SR3 cut, so it cannot
   accidentally certify an earlier, weaker pre-cut LP snapshot.
2. **Pricing** — a dynamic-programming label-setting algorithm finds new,
   improving bins exactly, in scaled-integer arithmetic, rather than
   approximating with a heuristic.
3. **SR3 (triplet) cut separation** — automatically strengthens the bound
   with cuts on triples of items, run to a bounded number of simultaneous
   cuts so this stays fast (see `docs/STATUS.md` for how).
4. **Root-node exact certification** — when cuts alone don't close the
   gap, a genuine 0/1 covering problem is solved on the enumerated bins;
   failing to find anything strictly better than the current best is
   itself a complete proof that it's optimal.
5. **Ryan-Foster branch-and-price** — the fallback for the (rare) instances
   the root alone cannot certify: a full enumeration tree with the same
   exact pricing and cut machinery at every node.
6. **Exact one-bin-gap certification on the populated pool** — when the
   SR3-strengthened root bound still leaves a one-bin gap, the code fixes the
   populated columns and asks whether they can cover all items using at most
   `incumbent - 1` bins. A Ryan-Foster branch-and-bound proves the answer over
   this finite pool, with pricing deliberately disabled; infeasibility proves
   that the incumbent is optimal, rather than merely failing to improve it
   heuristically.

## Decisive SR3-to-rational improvement

The rational certificate is now computed on the *same final restricted
master* strengthened by SR3 separation. This matters because a few hard ANI600
instances gain only a tiny amount above an integer from their triplet cuts. A
stale pre-triplet model can lose that gain and fail to certify optimality; the
current in-memory hand-off preserves it. For example, `600_20000_NR_0` obtains
the safe bound `198.0000015454603` after four SR3 cuts, whose ceiling proves
the incumbent value 199. This synchronization is a decisive implementation
improvement behind the complete ANI600 coverage in the reported campaign.

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
