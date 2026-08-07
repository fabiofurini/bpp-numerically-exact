# Historical `info_*` baselines

These append-only text files were produced by the original BPP executable.
They are kept as evidence for the refactoring comparisons, not as inputs to
the new solver. The files are separated from the project root so that the
source tree contains no misleading runtime output.

| file | contents | why it is kept |
| --- | --- | --- |
| `info_EXTRA.txt` | compact per-instance columns, LP/UB, node, triplet and timing fields | baseline for columns, bounds, triplets and performance |
| `info_Exensive.txt` | verbose per-run diagnostics and status information | investigate numerical failures and legacy termination decisions |
| `info_HEUR.txt` | first-fit/best-fit and other primal heuristic values | compare incumbent construction and primal heuristics |
| `info_Incumbent.txt` | incumbent packing/bin composition | validate reconstructed primal solutions |
| `info_REDUCTION.txt` | preprocessing reductions, fixed items and fixed bins | validate preprocessing and lower-bound changes |

The `ani-baseline-info_*` files are the separate two-row exports found in the
parent `BPP_ITALIA` directory beside the paper. They use the historical
no-SoPlex/no-populate parameter file and contain the authoritative ANI201 and
ANI402 reference rows used by `tests/results/ani-baseline.csv`:

- `ani-baseline-info_EXTRA.txt` and `ani-baseline-info_Exensive.txt` contain
  compact/verbose bounds, columns, triplets, nodes and timings;
- `ani-baseline-info_HEUR.txt`, `ani-baseline-info_Incumbent.txt` and
  `ani-baseline-info_REDUCTION.txt` contain the corresponding heuristic,
  incumbent and preprocessing fields.

The first rows contain the historical SoPlex/populate experiment in some
variants; the no-SoPlex/no-populate reference is documented in
[`../legacy-info-files.md`](../legacy-info-files.md) and
[`../legacy-parameter-matrix.md`](../legacy-parameter-matrix.md). The files are
not read by the refactored executable. The legacy executable may recreate files
with the same names in its working directory when it is run.
