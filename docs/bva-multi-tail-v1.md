# Generalized bounded variable addition — matching clause tails

**Branch:** stacked on the independently certified binary-only BVA PR #20.

## Problem found by executed measurement

In [two-family same-binary run 39083301554](https://github.com/SaridakisStamatisChristos/SatSolv/actions/runs/39083301554), the narrow binary-clause BVA implementation introduced 16 variables (saving 730 binary literal occurrences) on `posixpath` but introduced *zero* on `or_randxor`. Both control and candidate timed out at 60 s, giving no certified solve improvement.

This is an evidence-based reason to expand the **pattern class**, not to increase arbitrary runtime thresholds or claim the candidate solved more instances.

## New implementation

Generalize the original binary biclique pass to complete `2 x k` *clause* rectangles. For literals `a,c` and distinct clause tails `T_1 .. T_k` (`k >= 3`), require all original clauses `(a | T_i)`, `(c | T_i)`. Replace the selected clauses with

- `(a | x)`;
- `(c | x)`;
- `(-x | T_i)` for each `i`;

where `x` is fresh. Here `T_i` can be a single literal **or 2–7 literals**, unlike PR #20.

The existential equivalence follows by distributing `(a | T_i)` and `(c | T_i)` over the binary witness `x`, and conversely by eliminating `x` from the two direction clauses and each tail clause.

The independent proof contract remains exactly the same: `(x a)`, `(x c)` are fresh-variable RAT additions; each `(-x T_i)` is RAT because its resolvents are existing `(a T_i)`, `(c T_i)` clauses. The first literal is the RAT pivot. The original source clauses are not deleted from the proof checker. The SAT witness `x` is discarded only after the internal solver reconstructs all assignments and independently checks the original CNF.

## Resource boundaries

- At most 300,000 input clauses, clause length 2–8 for candidates;
- max 64 alternative center literals per normalized tail, 32 tails per accepted rectangle, 16 fresh variables;
- signed/two-polarity centers sharing one variable are excluded;
- deterministic remaining-work counter applies to clause indexing and pair traversal;
- incomplete indexing or interrupted candidate construction emits no proof additions or rewrite;
- all pre-existing search/default functionality unchanged; feature remains gated on `--experimental-bva`.

This is still **initial** structural rewriting, not Kissat's full quotient factoring or recurring inprocessing. The default solver remains unchanged.

## Validation

- C++ existential truth table exhaustively checks all 32 assignments for binary and all 64 for multi-literal patterns;
- strict RAT pivot order and literal savings assertions;
- extended 80-case proof/model checker alternates binary and multi-literal rectangles, with independent original-CNF SAT model validation and pinned `drat-trim` for UNSAT;
- standard release, sanitizer, and MiniSat/Kissat differential CI;
- selected `or_randxor` and `posixpath` same-binary diagnostic retained unchanged and interpreted as diagnostic, not a holdout or superiority claim.

Any certification failure blocks this PR. An unchanged timeout does not count as a win. Broader performance claims require the full frozen-30, a valid disjoint holdout, repeated seeded evaluation and contemporary comparators.
