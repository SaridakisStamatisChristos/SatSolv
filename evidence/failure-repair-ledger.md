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
- Root cause: the pinned MiniSat 2.2.0 repository expects `MROOT` plus the `simp/` makefile target; the initial CI recipe assumed a newer top-level build convention.
- Earliest invalidated phase: INTEGRATE/PACKAGE (external baseline build recipe), not solver logic.
- Repair: export `MROOT=/tmp/minisat`, build from `/tmp/minisat/simp`, and use the optimized release binary.
- Reverification: latest-head CI run `38205848811` completed the pinned MiniSat/Kissat differential lane successfully.

## F-004 — Benchmark JSON dropped raw solver streams

- Reproducer: R7 integration audit against the benchmark-integrity contract.
- Symptom: the initial harness measured stdout/stderr but omitted them when serializing per-solver rows.
- Root cause: summary-oriented result projection discarded fields returned by the process runner.
- Earliest invalidated phase: INTEGRATE for benchmark evidence retention.
- Repair: retain command, exit code, complete stdout/stderr, timing, timeout/crash state, independent verification transcript, proof size, tool versions, repository commit, and manifest hash in raw JSON; add `report_benchmark.py` to derive the required Markdown table.
- Reverification: local smoke harness solved and independently verified all committed smoke cases. Latest-head CI run `38205848811` reran the certified smoke harness successfully.

## F-005 — Verified DRAT proof misclassified by exit-code assumption

- Reproducer: GitHub Actions run `38177405010` on head `7a7a847e125a8b244f4fcb8847f1c8301370d2fb`.
- Symptom: all three external `drat-trim` checks printed `s VERIFIED`, but the CI step failed after the trivial empty-clause case because that checker invocation returned exit code 1.
- Root cause: the integration treated process exit code 0 as the sole acceptance signal instead of consuming the checker's explicit verification verdict.
- Earliest invalidated phase: INTEGRATE/VERIFY evidence interpretation, not proof generation.
- Repair: require an exact `s VERIFIED` line; retain the raw checker exit code/stdout/stderr in evidence. The wrapper rejects missing/other verdicts regardless of return code.
- Reverification: latest-head CI run `38205848811` completed all three upstream `drat-trim` smoke certifications successfully.

## F-006 — MiniSat 2.2.0 rejects modern GCC friend-declaration diagnostics

- Reproducer: GitHub Actions run `38177405010`, Ubuntu 24.04 / GCC 13.3, exact MiniSat commit `eb01ad68b75bb3b34ff8657c37ad6a31faae0fc3`.
- Symptom: the upstream 2.2.0 source fails with `friend declaration ... specifies default arguments` errors before differential testing.
- Root cause: legacy C++ accepted by MiniSat's era is diagnosed as an error by modern GCC unless permissive compatibility mode is enabled.
- Earliest invalidated phase: INTEGRATE/PACKAGE baseline portability, not solver logic.
- Repair: keep the exact upstream source untouched and build its optimized release target with `CXX="g++ -fpermissive"`; use `minisat_release` as the baseline binary.
- Reverification: latest-head CI run `38205848811` built the pinned MiniSat commit and completed the 200-case differential campaign successfully.

## F-007 — Full acceptance workflow was false-green on rejected certification

- Reproducer: Acceptance Benchmark run `38178398630` on head `bd1d62afbf5f6f6617853e21cdff252defbb5667`.
- Symptom: the workflow concluded `success` even though its raw 30-instance report contained one SatSolv verification failure. SatSolv returned UNSAT for `clqcolor-08-06-07.shuffled-as.sat05-1257.cnf`, produced a 38,155,990-byte DRAT proof, and the independent checker did not reach `s VERIFIED` within its then-current timeout. MiniSat and Kissat also returned UNSAT, so this was a certification failure rather than evidence of a wrong logical result.
- Root cause: `tools/benchmark.py` recorded verification failures in JSON/summary metrics but never converted a certification violation into a failing process exit status. The workflow therefore treated evidence generation as acceptance.
- Earliest invalidated phase: VERIFY. The solver result remained uncertified; Gate F could not be considered passed despite green Actions UI.
- Repair: commit `bd1aaedac85e89013dad6e3068160176a16737c9` adds an explicit certification verdict and exits with status 3 on unverified SatSolv SAT/UNSAT answers, wrong answers, SatSolv crashes, or baseline/manifest contradictions.
- Falsification guard: `tools/benchmark_fail_closed_check.py` deliberately feeds the harness an UNSAT result whose proof is rejected and requires exit 3, preserved JSON evidence, `certification.passed=false`, and a structured `verification_failure`. Latest-head CI run `38205848811` passed this regression.
- Reverification: full competition acceptance run `38205616073` is the authoritative post-repair run and remains pending until its raw artifact is inspected. No pass is claimed here before that artifact exists.

## F-008 — Solver runtime and proof-checker runtime were incorrectly coupled

- Reproducer: the same acceptance run `38178398630`; SatSolv reached the UNSAT result in about 14.31 s, emitted a 38.2 MB proof, but `drat-trim` was forced to share the 60 s competitive solver budget and timed out.
- Symptom: a solver result that met the 60 s solver requirement could not be certified solely because independent proof checking was constrained by an unrelated competitive-runtime budget.
- Root cause: `verify_unsat()` reused `--time-limit`, which was intended to measure solver performance, as the proof-verification timeout.
- Earliest invalidated phase: SPEC/VERIFY boundary for acceptance-resource semantics.
- Repair: the harness now has an independent `--verification-time-limit`; the acceptance workflow pins it to 300 s while preserving the 60 s per-solver competitive budget and 4 GiB solver memory budget. Commit `dae43af4381791e29f2d20911d7d9091ebe6200b` triggers the post-repair acceptance run.
- Reverification: pending raw evidence from acceptance run `38205616073`. If `drat-trim` still does not emit exact `s VERIFIED`, the gate remains failed by construction.
