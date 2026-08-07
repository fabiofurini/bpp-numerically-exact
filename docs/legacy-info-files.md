# Legacy `info_*` files

The historical BPP executable writes these files in its working/result
directory. They are not input data and are not required to run the solver; they
are append-only regression/oracle files used to verify that the refactoring
preserves the old behavior. The preserved copies now live in
[`legacy-info/`](legacy-info/), outside the project root.

There are two historical baselines in the checked-in logs, and they must not
be mixed:

- the first two rows of the project-local files use
  `param_test_BPP_ANI_si_SOPLEX_si_POP.txt` (SoPlex and enumeration/populate
  enabled);
- the parent-directory rows use
  `param_test_BPP_ANI_no_SOPLEX_no_POP.txt` (SoPlex and populate disabled).

The authoritative two-row no-populate exports from the paper directory are in
`legacy-info/ani-baseline-info_EXTRA.txt` and
`legacy-info/ani-baseline-info_Exensive.txt`. They record ANI201/ANI402 incumbent, status, runtime,
column count, LP value, phase counters, pricing counters, triplets and tree
statistics. The preserved files contain the larger append-only history of later
experiments, including the two SoPlex/populate rows above.

The auxiliary files are:

- `legacy-info/ani-baseline-info_HEUR.txt`: first-fit and best-fit heuristic values;
- `legacy-info/ani-baseline-info_Incumbent.txt`: incumbent packing shape and bin statistics;
- `legacy-info/ani-baseline-info_REDUCTION.txt`: preprocessing fixed items, fixed bins and reduction
  lower bound.

The refactored no-populate comparison uses the `ani-baseline-info_*` rows in
`legacy-info/` as the
baseline and does not treat any `info_*` value as a solver input. The historical
SoPlex/populate rows remain useful regression data, but are reported separately
until the populate path is explicitly enabled and validated.
