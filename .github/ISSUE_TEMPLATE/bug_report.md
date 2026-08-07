---
name: Bug report
about: Something in the solver, build, or CLI behaves incorrectly
title: ''
labels: bug
assignees: ''
---

**Describe the bug**
A clear description of what went wrong.

**Instance and command**
The exact `bpp-solve` command line (or library call) and, if possible, the
instance file (or a minimal one that reproduces it).

**Expected vs. actual output**
What you expected `bpp-solve` to report, and what it reported instead
(paste the full key/value output).

**Build**
- `build/` (portable) or `build-cplex-soplex/` (CPLEX+SoPlex)?
- CMake configure line used
- Output of `ctest --output-on-failure` on your machine

**Additional context**
Anything else relevant (OS, compiler, CPLEX/SoPlex version).
