# R7 requirement ledger

This ledger is intentionally evidence-stateful. Presence of code is not verification, and a green workflow is not certification unless the underlying artifact satisfies the stated oracle.

Evidence anchors used below:

- Latest fully observed CI evidence: Actions run `38205848811` — build/certify, ASan+UBSan, exhaustive/random/metamorphic checks, adversarial parser checks, independent SAT model validation, upstream `drat-trim` smoke certification, fail-closed benchmark regression, and the 200-case pinned MiniSat/Kissat differential lane all passed.
- Original full acceptance run `38178398630` is **not** passing evidence: its artifact contained one SatSolv verification failure even though Actions reported success. See `evidence/failure-repair-ledger.md` F-007/F-008.
- Authoritative repaired full acceptance run: `38205616073` at commit `dae43af4381791e29f2d20911d7d9091ebe6200b`. Its input-fetch/hash gate has passed and its certified comparison is still running. No final acceptance claim is made until its raw artifact is inspected.

| ID | Hard requirement | Evidence required | Current state |
|---|---|---|---|
| R1 | Native CDCL, no solver wrapping | source inspection + unit/exhaustive tests | **VERIFIED** — native C++ CDCL core inspected; CI unit and exhaustive/random lanes pass |
| R2 | Robust DIMACS parser | unit + adversarial parser tests | **VERIFIED** — CI malformed/adversarial parser lane passes |
| R3 | Two-watch BCP | unit + exhaustive/random/metamorphic | **VERIFIED** — implementation inspected; all three CI correctness modes pass |
| R4 | First-UIP + backjump + learning | conflict tests + exhaustive/random | **VERIFIED** — implementation inspected and exercised by exhaustive/random/metamorphic campaigns |
| R5 | EVSIDS/phase/restarts/reduction/LBD | source + exercised stats/tests | **VERIFIED** — implementation inspected; prior stress evidence exercised restarts, learning and learnt-clause deletion; current correctness lanes remain green |
| R6 | SAT witness soundness | independent model oracle | **VERIFIED** — independent model checker passes in latest CI |
| R7 | DRAT for every certified UNSAT | proof files + checker | **PARTIAL / REVERIFYING** — CI smoke UNSATs are certified; full competition-slice UNSAT certification awaits run `38205616073` |
| R8 | `drat-trim` accepts UNSAT proofs | external `drat-trim` logs | **PARTIAL / REVERIFYING** — upstream checker passes smoke proofs; prior full run timed out on one 38.2 MB proof; repaired run uses an independent 300 s checker budget and is pending |
| R9 | Differential consistency | MiniSat + Kissat differential run | **VERIFIED** — latest CI passes the pinned 200-case differential campaign |
| R10 | Exhaustive/property checks | brute-force + metamorphic logs | **VERIFIED** — latest CI passes exhaustive/random and metamorphic lanes |
| R11 | Sanitizer/static checks | ASan/UBSan + strict compiler | **VERIFIED** — ASan+UBSan lanes pass; normal build uses `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` |
| R12 | Fixed SAT Competition slice with SHA-256 | resolved manifest + fetch/hash check | **VERIFIED** — 30-instance resolved manifest exists and repaired acceptance run passed the fetch/hash gate |
| R13 | Resource-limited benchmark + raw results | benchmark JSON | **REVERIFYING** — prior raw artifact exposed a certification failure; repaired fail-closed run is executing with 60 s solver and 300 s proof-check budgets |
| R14 | Honest Kissat/MiniSat comparison | pinned builds + raw results | **REVERIFYING** — pinned baseline builds succeeded in the repaired acceptance run; final comparative artifact is pending |
| R15 | CI mirrors critical gates | observed GitHub Actions run | **VERIFIED** — latest CI includes positive and adversarial certification paths, sanitizers, independent oracles, and pinned differential testing |

## Acceptance rule

R7 is not complete while any hard row is `PARTIAL`, `REVERIFYING`, `FAILED`, or `UNVERIFIED`. In particular, Actions UI color is never sufficient evidence for R7/R8/R13/R14: the raw acceptance JSON must have `certification.passed=true`, no structured certification violations, zero SatSolv verification failures, and independently validated SAT/UNSAT rows.
