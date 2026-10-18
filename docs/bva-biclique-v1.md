# Experimental bounded variable addition (BVA): binary bicliques v1

## Motivation and exact scope

The pinned Kissat 4.0.4 ablations established that removing its `factor` feature causes major regressions on `or_randxor` and `posixpath`. This is *not* evidence that a generic BVA implementation will fix SatSolv: Kissat's implementation uses more general quotient/structural factoring and scheduled recurring inprocessing. This experimental module implements **only** a bounded complete *binary clause biclique* transformation, as a correctness-first primitive. It is **OFF by default**; neither prior structural-search failures nor the qualified target-phase baseline are changed.

## Algebraic contract

Given distinct literals `a, c` and distinct `b_1 .. b_k`, `k >= 3`, with **all** input binary clauses

`(a | b_i)` and `(c | b_i)`, for each `i`,

replace these `2k` clauses by

`(a | x), (c | x), (-x | b_1), ..., (-x | b_k)`

where `x` is a fresh variable. The result is existentially equivalent under projection to the old variable set:

`forall original assignments v: F(v) iff exists x: F'(v,x)`.

The transformation reduces this clause biclique from `2k` to `k+2` binary clauses. A bounded search selects a group with `k>=3`, charges candidate indexing work, limits neighbor degree to 64, chooses up to 32 common neighbors and introduces at most 16 extension variables per invocation. More general Kissat-style factoring is deliberately **not** asserted.

### Proof ordering — RAT, not merely RUP

An external DRAT checker starts from the **original** CNF. The emitted proof additions, with pivot first, are:

1. `(x a)`, `(x c)`: RAT on fresh positive `x`, with no negative `x` clauses.
2. For each `b_i`, `(-x b_i)`: RAT on `-x`; its resolvents with `(x a)` and `(x c)` are the original `(a b_i)`, `(c b_i)`, hence RUP.

Original biclique clauses are retained in the verifier proof database, so no unsound deletion shortcut is needed. Subsequent transformations each allocate a new fresh variable. A partial candidate scan does **not** commit a rewrite; cancellation before proof emission is a safe no-op. The solver never claims a definitive UNSAT from a failed/exhausted scan.

### SAT model reconstruction

Internal CDCL works on all original and extension variables. Congruence/other existing transformations retain their normal reverse extension steps. Only **after** they restore the internal assignment is the SAT output projected to the original variable count. The original CNF model is independently validated; extension witness literals must not appear in printed SAT output.

### Interfaces and resource gates

```
--experimental-bva
--bva-budget 2000000
--bva-max-variables 16
```

Counters: `bva_work`, `bva_variables_added`, `bva_clauses_removed`, `bva_literals_saved`. A zero budget, a zero variable allowance, or omitted `--experimental-bva` results in no BVA changes. Overlarge formulas fall back to the existing engine. This feature is a **pre-search** standalone proof mechanism; it does NOT yet implement recurring inprocessing, SAT sweeping, full Kissat BVA, or any automatic policy selection.

## Verification and acceptance

- Native exhaustive truth-table test: all 32 original assignments for a 5-variable biclique, existentially quantified over both `x` assignments.
- C++ tests: literal/variable counts, proof pivot order, interrupted scan/no-output, budget exhaustion, malformed input, SAT/UNSAT, model projection, zero-budget.
- CI under Release/ASan/UBSan via standard repository workflow.
- Independent randomized 80-instance campaign: same solver binary with opt-in candidate, control, and zero-budget control. Independent model checker and pinned external `drat-trim` on **original CNFs**, never on the transformed formula.
- Exact frozen benchmark acceptance remains separate. Success of this PR's correctness checks does **not** prove a performance win. A separate same-binary, frozen full-30 plus disjoint external held-out comparison is required before adoption or a claim of comparative superiority.

### Implementation decisions

This BVA primitive is a reversible, flag-gated experimental implementation authorized for investigation, not for merger into the validated mainline search/default configuration. Any performance regression or proof failure is a hard blocker for adoption; any proof/model failure is a hard blocker even for diagnostic status.
