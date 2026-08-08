# Output format

[← Back to README](../README.md) · [← Usage](USAGE.md) · [Input format](INPUT.md)

Everything a run produces is written to **one place**: `key value` pairs,
one per line, on **stdout**. There are no auxiliary result files, no
per-run logs, and no scattered output directories to track down — a
caller that wants machine-readable results just captures stdout (see
`scripts/run_ani_comparison.sh` for an example consumer). Diagnostics and
errors go to **stderr**, never stdout, so the two never need separating
after the fact.

## No mode (greedy fallback) example

The only mode that runs without a CPLEX- or Gurobi-enabled build, and the
only one whose output isn't `key value` pairs — it's the actual solution.
On the 5-item, capacity-10 instance from [Input format](INPUT.md):

```
$ bpp-solve instance.txt
bins 2
10: 0 2
10: 1 3 4
```

| Line | Meaning |
|---|---|
| `bins N` | Number of bins the heuristic used — not certified optimal, just feasible. |
| One line per bin | `<load>: <item indices>` — the total weight placed in that bin, then every item (by its 0-based index in the instance file) assigned to it. |

## `--root-cg` / `--populate` / `--legacy-root-cg` / `--no-populate` example

All four modes share this same field set (`--legacy-root-cg` and
`--no-populate` are aliases for the same historical no-cuts, no-safe-phase
path; `populate_columns`/`sr3_cuts_added` are simply 0 where the mode
doesn't use them):

```
$ bpp-solve instance.txt --root-cg
status converged
lp_bound 65
upper_bound 66
incumbent 66
lower_bound_safe 66
lower_bound_safe_ceil 66
integer_optimum_certified 1
safe_bound 66
safe_duals_feasible 1
iterations 394
phase1_iterations 394
phase2_iterations 0
populate_columns 0
populate_complete 1
sr3_cuts 4
sr3_cuts_added 4
automatic_sr3 1
stabilization 0
solver cplex
phase2_verified 1
phase2_backend soplex
columns 4465
generated_columns 4264
```

| Field | Meaning |
|---|---|
| `status` | `converged` if pricing certified optimality of the relaxation, `iteration_limit` if it ran out of iterations first. |
| `lp_bound` | The current (possibly not yet certified) LP relaxation value. |
| `incumbent` / `upper_bound` | Best integer (feasible) solution found — a valid number of bins, always a true upper bound. |
| `safe_bound` / `lower_bound_safe` | The certified, numerically exact lower bound, as an exact rational number. |
| `lower_bound_safe_ceil` | That bound rounded up — the true value only an integer solution could reach. |
| `integer_optimum_certified` | **1** if `lower_bound_safe_ceil == incumbent`: the incumbent is *proven* optimal, not just the best one found. **0** otherwise — the incumbent may still be optimal, but this run did not prove it (try `--populate` or `--branch-price`). |
| `safe_duals_feasible` | Whether the exact certificate above is valid (should always be 1 on a successful run). |
| `iterations` / `phase1_iterations` / `phase2_iterations` | Column-generation iteration counts (floating-phase vs. rational-phase). |
| `populate_columns` / `populate_complete` | How many extra patterns `--populate` enumerated, and whether the enumeration finished (vs. was cut off by its own bounds). |
| `sr3_cuts_added` | How many SR3 (triplet) cutting planes were separated. |
| `solver` | Which floating-point LP backend actually ran (`cplex` or `gurobi`). |
| `phase2_backend` | Which solver certified the safe phase (`soplex` in the numerically exact build). |
| `columns` / `generated_columns` | Total patterns in the final pool, and how many were generated (as opposed to the initial singleton seed). |

## `--branch-price` example

```
$ bpp-solve instance.txt --branch-price 50
status optimal
strategy best-bound
diving 0
stabilization 0
solver cplex
incumbent 66
lower_bound_safe 66
lower_bound_safe_ceil 66
integer_optimum_certified 1
processed_nodes 1
generated_nodes 0
pruned_nodes 1
```

| Field | Meaning |
|---|---|
| `status` | `optimal` if the search tree closed completely, `node_limit` if `MAX_NODES` was reached first. |
| `strategy` | Node exploration order used (`best-bound` or `depth-first`). |
| `processed_nodes` / `generated_nodes` / `pruned_nodes` | Branch-and-price tree size and how much of it was fathomed without full exploration. |
| (other fields) | Same meaning as the `--root-cg` table above, at the tree-wide (not just root) level. |

---

See also: [Input format](INPUT.md) · [Usage](USAGE.md)
