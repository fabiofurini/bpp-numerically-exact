# Documentation map

## User-facing

- [`CODE.md`](CODE.md): repository layout, what implements what.
- [`COMPILE.md`](COMPILE.md): dependencies and build instructions.
- [`USAGE.md`](USAGE.md): how to invoke `bpp-solve`, modes and options.
- [`EXAMPLE.md`](EXAMPLE.md): a worked example, reading a solution figure bin by bin.
- [`INPUT.md`](INPUT.md): instance file format.
- [`OUTPUT.md`](OUTPUT.md): output fields, with worked examples.

## Engineering / development history

- [`STATUS.md`](STATUS.md): refactoring checkpoint, verified work and remaining gaps.
- [`CONTINUATION_STATE.md`](CONTINUATION_STATE.md): machine-readable handoff for continuing the refactoring.
- [`legacy-feature-inventory.md`](legacy-feature-inventory.md): migration contract against the old BPP code.
- [`legacy-parameter-matrix.md`](legacy-parameter-matrix.md): historical parameter variants.
- [`legacy-info-files.md`](legacy-info-files.md): interpretation of the legacy runtime files.
- [`legacy-info/`](legacy-info/): preserved long histories and the two-row ANI baseline exports.
- [`ani201-full-comparison.md`](ani201-full-comparison.md): full 50/50 ANI-201 comparison against the official reference binary, default flags only.
- [`ani402-10-comparison.md`](ani402-10-comparison.md): first 10 ANI-402 instances against the official reference binary, default flags only.
- [`ani-comparison-report.md`](ani-comparison-report.md): ANI comparison methodology and current results.
- [`paper-comparison.md`](paper-comparison.md): recalculation against the paper's Tables 1–3.

Generated build directories and per-run logs are intentionally not part of the
source tree. Recreate them with the commands in the top-level README.
