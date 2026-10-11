# DRAT proof system

`--proof FILE` enables certified proof emission. SatSolv logs all learnt clauses as ordinary DRAT additions and writes `0` for the final empty clause. Learnt-clause deletion is intentionally not logged: DRAT permits a checker to retain previously added clauses, and keeping them can only make later RUP checks easier. SatSolv currently emits a RUP-only DRAT subset, which is independently checkable by standard `drat-trim`.

The development helper `tools/check_rup.py` is deliberately independent from the C++ solver and catches proof-shape regressions quickly. It is not the acceptance authority. CI fetches the pinned upstream `drat-trim` revision `2e3b2dc0ecf938addbd779d42877b6ed69d9a985`, compiles it, and checks UNSAT cases externally.
Acceptance is keyed to the checker's exact `s VERIFIED` verdict, not a hard-coded process exit value: the pinned checker can emit `s VERIFIED` while returning a nonzero code on trivial-UNSAT input. The raw exit code and checker transcript are retained as evidence.

No preprocessing or inprocessing transformation is currently enabled. This is intentional: a transformation will be admitted only when its proof logging is implemented and the resulting trace is accepted by drat-trim.
