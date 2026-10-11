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

## F-003 — Pinned MiniSat CI build recipe used a non-existent target

- Reproducer: GitHub Actions run on Ubuntu 24.04 at pushed revision `797d7929d8217d79597a7cb5fe6e2b00aafaa76c`.
- Symptom: differential job stopped at `make -C /tmp/minisat -j2 r` with `No rule to make target 'r'`.
- Root cause: the pinned MiniSat 2.2.0 repository expects `MROOT` plus the `simp/` makefile target `rs`; the initial CI recipe assumed a newer top-level build convention.
- Earliest invalidated phase: INTEGRATE/PACKAGE (external baseline build recipe), not solver logic.
- Repair: export `MROOT=/tmp/minisat`, run `make -C /tmp/minisat/simp -j2 rs`, and copy `simp/minisat_static`.
- Reverification: requires the repaired GitHub Actions differential lane to complete successfully.

## F-004 — Benchmark JSON dropped raw solver streams

- Reproducer: R7 integration audit against the benchmark-integrity contract.
- Symptom: the initial harness measured stdout/stderr but omitted them when serializing per-solver rows.
- Root cause: summary-oriented result projection discarded fields returned by the process runner.
- Earliest invalidated phase: INTEGRATE for benchmark evidence retention.
- Repair: retain command, exit code, complete stdout/stderr, timing, timeout/crash state, independent verification transcript, proof size, tool versions, repository commit, and manifest hash in raw JSON; add `report_benchmark.py` to derive the required Markdown table.
- Reverification: local smoke harness solved and independently verified all 5 committed smoke cases, retained both output streams for every SatSolv row, and generated the Markdown table from JSON. CI reruns the smoke harness with upstream drat-trim.

## F-005 — Verified DRAT proof misclassified by exit-code assumption

- Reproducer: GitHub Actions run `38177405010` on head `7a7a847e125a8b244f4fcb8847f1c8301370d2fb`.
- Symptom: all three external `drat-trim` checks printed `s VERIFIED`, but the CI step failed after the trivial empty-clause case because that checker invocation returned exit code 1.
- Root cause: the integration treated process exit code 0 as the sole acceptance signal instead of consuming the checker's explicit verification verdict.
- Earliest invalidated phase: INTEGRATE/VERIFY evidence interpretation, not proof generation.
- Repair: require an exact `s VERIFIED` line; retain the raw checker exit code/stdout/stderr in evidence. The wrapper rejects missing/other verdicts regardless of return code.
- Local revalidation: a mock checker emitting `s VERIFIED` with exit 1 is accepted, and the 5-case certified smoke harness reports 5/5 solved with zero verification failures. External CI revalidation remains required.

## F-006 — MiniSat 2.2.0 rejects modern GCC friend-declaration diagnostics

- Reproducer: GitHub Actions run `38177405010`, Ubuntu 24.04 / GCC 13.3, exact MiniSat commit `eb01ad68b75bb3b34ff8657c37ad6a31faae0fc3`.
- Symptom: the upstream 2.2.0 source fails with `friend declaration ... specifies default arguments` errors before differential testing.
- Root cause: legacy C++ accepted by MiniSat's era is diagnosed as an error by modern GCC unless permissive compatibility mode is enabled.
- Earliest invalidated phase: INTEGRATE/PACKAGE baseline portability, not solver logic.
- Repair: keep the exact upstream source untouched and build its optimized `r` target with `CXX="g++ -fpermissive"`; use `minisat_release` as the baseline binary.
- Reverification: requires the next GitHub Actions differential lane.
