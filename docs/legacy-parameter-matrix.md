# Historical BPP parameter variants

The archived BPP setup contains more than one ANI configuration. The value of
`PARAM_SOPLEX` is not inferred from the executable name: it is part of the
parameter file and changes the numerical phase of the old solver.

| Parameter file family | SoPlex | Populate/enumeration | Scale | Refactored mapping |
| --- | ---: | ---: | ---: | --- |
| `param_test_BPP_ANI_no_SOPLEX_no_POP.txt` | 0 | 0 | `1e13` | `--legacy-root-cg` (baseline used by the no-populate ANI comparison) |
| `param_test_BPP_ANI_no_SOPLEX_si_POP.txt` | 0 | 1 | `1e13` | not enabled in the no-populate target |
| `param_test_BPP_ANI_si_SOPLEX_si_POP.txt` | 1 | 1 | `1e13` | `--root-cg` on the SoPlex build, with populate still a separate validation task |
| `param_test_BPP_ANI_paper_v2*.txt` | 1 | 1 | `1e13` | same SoPlex/populate family |
| `param_test_BPP_ANI_si_SOPLEX_si_POP_v2..v5.txt` | 1 | 1 | `1e12..1e9` | same phase, different safe scaling experiments |

The old files also keep the common BPP controls that define the pricing
behavior: batch size 10, dominance and fathoming enabled, three historical
fathoming tests enabled, fourth disabled, triplet gap 2, ten triplets per
iteration, no smoothing, and most-fractional Ryan--Foster branching. The new
defaults use the same values where that path is implemented.

The first two rows of the preserved long `docs/legacy-info/info_*` files are
the SoPlex/populate run. The separate
`docs/legacy-info/ani-baseline-info_EXTRA.txt` and
`ani-baseline-info_Exensive.txt` files are the two-row no-SoPlex/no-populate
baseline exported from the paper directory. The ANI comparison CSV intentionally uses the latter and runs
`--legacy-root-cg`; safe SoPlex/GMP runs are separate diagnostics, not silently
substituted into that baseline.
