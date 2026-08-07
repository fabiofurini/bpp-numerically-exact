# Usage: calling the solver

[← Back to README](../README.md) · [← Build & Compile](BUILD.md)

## Command syntax

```
bpp-solve INSTANCE
bpp-solve INSTANCE --root-cg       [MAX_ITERATIONS] [OPTIONS]
bpp-solve INSTANCE --no-populate   [MAX_ITERATIONS] [OPTIONS]
bpp-solve INSTANCE --legacy-root-cg [MAX_ITERATIONS] [OPTIONS]
bpp-solve INSTANCE --populate      [MAX_COLUMNS]     [OPTIONS]
bpp-solve INSTANCE --branch-price  [MAX_NODES]       [OPTIONS]
bpp-solve --help | -h
```

Run `bpp-solve --help` at any time for the full, authoritative flag
reference (kept in sync with the binary itself). This page summarizes it.
See [Input format](INPUT.md) and [Output format](OUTPUT.md) for what
`INSTANCE` looks like and what the solver prints back.

## Modes

| Mode | What it does |
|---|---|
| *(none)* | Greedy first-fit-decreasing heuristic. Not exact; a quick sanity check, or the only option in a portable (no-CPLEX/Gurobi) build. |
| `--root-cg` | Floating column-generation phase, then the mandatory rational SoPlex/GMP certification phase. Automatic SR3 cut separation enabled. **This is the numerically exact root relaxation.** |
| `--no-populate` / `--legacy-root-cg` | Floating root phase only — no safe-phase certification, no SR3 separation. Historical compatibility path. |
| `--populate` | Like `--root-cg`, followed by bounded pattern enumeration and an exact MIP-based certification step (proves optimality even when the root LP bound alone cannot) up to `MAX_COLUMNS`. |
| `--branch-price` | Full Ryan–Foster branch-and-price tree to a certified integer optimum, or until `MAX_NODES` is reached. |

Most instances certify already at `--root-cg`; `--populate` and
`--branch-price` are what to reach for when they do not.

## Useful options

- `--strategy best-bound\|depth-first` (`--branch-price` only) — node
  exploration order; both exact.
- `--solver cplex\|gurobi` — which floating-point LP backend to use;
  defaults to whichever the binary was built with.
- `--sr3-gap-activation VALUE`, `--sr3-max-cuts VALUE`, `--sr3-max-rounds N`
  — tune the SR3 cutting-plane schedule.
- `--diving`, `--stabilization` — opt-in historical heuristics, off by
  default so the default path matches exactly what the paper describes.

Full detail on every option: `bpp-solve --help`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success: converged/optimal with a certified bound, or the greedy fallback ran without error. |
| 1 | Runtime error (invalid instance file, solver exception, etc.). |
| 2 | Usage error (missing/invalid arguments). |
| 3 | The iteration/node limit was reached before reaching a certified result. |

## Examples

```sh
bpp-solve instance.txt
bpp-solve instance.txt --root-cg 500
bpp-solve instance.txt --branch-price 200 --strategy depth-first --diving
bpp-solve instance.txt --populate 5000 --sr3-max-cuts 20
bpp-solve instance.txt --root-cg --solver gurobi
```

---

Next: [Input format](INPUT.md) · [Output format](OUTPUT.md)
