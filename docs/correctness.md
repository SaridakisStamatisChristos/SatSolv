# Correctness contract

Hard invariants:

- parser errors never become logical answers;
- a propagated assignment has a valid reason clause; decisions have no reason;
- every active non-unit clause has two valid watch positions and each watch-list entry names the literal it watches;
- backtracking clears assignments/reasons above the target decision level and never removes level-0 assignments;
- learnt clauses are produced by First-UIP resolution and are implied by the active clause database;
- clause minimization is allowed only through explicit sound resolution-subset elimination;
- locked reason clauses and binary clauses are not removed by learnt-database reduction;
- UNSAT is emitted only from an explicit root contradiction;
- timeout produces UNKNOWN;
- every counted SAT result must pass the independent Python model checker;
- every counted UNSAT result must pass external drat-trim.

The test stack contains native unit tests, exhaustive two-variable formula-space coverage over a canonical clause universe, seeded randomized formulas validated by brute force, metamorphic invariants, malformed-parser attacks, sanitizer builds, differential tests against external solvers, and external DRAT checking.
