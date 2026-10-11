# Failure / repair ledger

## F-001 — Development RUP oracle rejected a valid root proof

- Reproducer: randomized formula containing duplicate literals plus root unit propagation.
- Symptom: the Python development RUP oracle rejected an empty-clause proof.
- Root cause: the oracle counted duplicate unassigned literals separately, so a logically unit clause such as `(-1 -3 -1)` did not become unit after assigning `3`.
- Earliest invalidated phase: VERIFY (oracle implementation), not solver generation.
- Repair: normalize duplicate literals and discard tautological original clauses in the independent RUP oracle.
- Reverification: 756 exhaustive/random formulas passed after repair.

## F-002 — Learnt minimizer composition risk found by proof-safety review

- Reproducer: code inspection; no observed wrong proof required.
- Risk: multiple removals were each checked against the original learnt-literal set. A later removal could therefore rely on a literal already removed earlier.
- Earliest invalidated phase: SPEC/GENERATE for clause minimization semantics.
- Repair: apply sequential resolution semantics by erasing each removed literal from the live justification set before considering subsequent removals.
- Reverification: 756 exhaustive/random formulas passed; pigeonhole 8/7 produced 4,121 RUP additions accepted by the independent development oracle, with 20 restarts and 2,497 learnt-clause deletions exercised.
