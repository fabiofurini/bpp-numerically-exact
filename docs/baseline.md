# Baseline archive build

The historical archive was copied to `/tmp/bpp_archive_baseline` before building; the source archive was not modified.

Command:

```sh
make clean
make -j2
```

Initial result: compilation stopped in `src/Main.cpp` and `src/DP.cpp` because the historical `src/global_variables.h` hard-coded an obsolete CPLEX path:

```text
/home/fabio/ILOG/CPLEX_Studio_AcademicResearch201/cplex/include/ilcplex/cplex.h
```

This is an environment/dependency failure, not yet an algorithmic comparison. A valid CPLEX installation (and subsequently the SoPlex/GMP libraries) is required before collecting archive output for regression. The new project's independent core currently builds and passes its tests without producing a misleading exact-solver result.

## Corrected temporary baseline

The machine has CPLEX 20 installed under `/home/fabio/ILOG/CPLEX_Studio201/`. In the temporary copy only, the include/library paths were corrected and the obsolete Gurobi link was removed. SoPlex 5.0.1 and GMP were found and linked successfully; the resulting `BPPS` executable contains SoPlex symbols and is 64-bit ELF.

SoPlex 5.0.1 also needs a one-line GCC-15 compatibility change in the temporary copy (`datahashtable.h`: array copy replaced by element-wise copy). The original archive and its vendored SoPlex sources remain untouched.

The binary launches and reaches instance parsing. The first attempted benchmark could not be used as a correctness run because the referenced `BPP_INST/Waescher/...` files are only represented by `BPP_INST/ALL.zip` in the archive copy; a second `.sbpp` file used a variant format and was rejected by the historical BPP reader. The next baseline step is therefore extracting/selecting a valid plain BPP instance and recording its output.
