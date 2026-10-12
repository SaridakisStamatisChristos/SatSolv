# R7 requirement ledger

This ledger is intentionally evidence-stateful. Presence of code is not verification, and a green workflow is not certification unless the underlying artifact satisfies the stated oracle.

Evidence anchors used below:

- Proof-correctness acceptance: Actions run `38205616073` at commit `dae43af4381791e29f2d20911d7d9091ebe6200b`. Its raw 30-instance artifact has `certification.passed=true`, no certification violations, and zero SatSolv SAT/UNSAT verification failures. The previously problematic 38,155,990-byte `clqcolor` UNSAT proof is accepted by pinned `drat-trim` under the independent 300 s verification budget.
- Runtime falsification: inspection of that same artifact exposed five SatSolv exit-code-3 `INTERNAL_ERROR: std::bad_alloc` cases that schema 3 had treated as ordinary UNKNOWN. Therefore run `38205616073` is valid proof/soundness evidence but is not sufficient to close the hard runtime-reliability gate. See F-009/F-010 in `evidence/failure-repair-ledger.md`.
- Memory repair: bounded stale EVSIDS-heap growth at source commit `dcf38aa3e14884882d0f2cd2e7821e79adfeed8a`. Focused Memory Probe Matrix run `38213459228` passes all five former allocator-failure cases under the same 4 GiB / 60 s contract; exact-source CI run `38213435636` passes sanitizers, correctness, DRAT, and differential gates.
- Hardened benchmark semantics: schema 4 at commit `fda3d3259896dde872ac3afe842dde7b7d2fc5f7` makes any non-timeout SatSolv exit outside `{0,10,20}` a `solver_error` certification violation. CI run `38213627773` passes the dedicated synthetic INTERNAL_ERROR fail-closed regression.
- Final full-corpus runtime/comparison gate: hardened Acceptance Benchmark run `38213565150` is executing. No final R11/R13/R14 closure is claimed before its raw artifact is inspected.

| ID | Hard requirement | Evidence required | Current state |
|---|---|---|---|
| R1 | Native CDCL, no solver wrapping | source inspection + unit/exhaustive tests | **VERIFIED** — native C++ CDCL core inspected; CI unit and exhaustive/random lanes pass |
| R2 | Robust DIMACS parser | unit + adversarial parser tests | **VERIFIED** — CI malformed/adversarial parser lane passes |
| R3 | Two-watch BCP | unit + exhaustive/random/metamorphic | **VERIFIED** — implementation inspected; all three CI correctness modes pass |
| R4 | First-UIP + backjump + learning | conflict tests + exhaustive/random | **VERIFIED** — implementation inspected and exercised by exhaustive/random/metamorphic campaigns |
| R5 | EVSIDS/phase/restarts/reduction/LBD | source + exercised stats/tests | **VERIFIED** — implementation inspected and exercised at scale; current correctness lanes remain green after memory repairs |
| R6 | SAT witness soundness | independent model oracle | **VERIFIED** — independent model checker passes in CI and full acceptance SAT solves are independently checked |
| R7 | DRAT for every certified UNSAT | proof files + checker | **VERIFIED** — full acceptance UNSAT results counted as solves only after independent proof certification; raw run `38205616073` has zero SatSolv verification failures |
| R8 | `drat-trim` accepts UNSAT proofs | external `drat-trim` logs | **VERIFIED** — pinned upstream checker accepts smoke proofs and the full-slice `clqcolor` proof; independent verification budget is 300 s |
| R9 | Differential consistency | MiniSat + Kissat differential run | **VERIFIED** — current CI passes the pinned 200-case differential campaign |
| R10 | Exhaustive/property checks | brute-force + metamorphic logs | **VERIFIED** — current CI passes exhaustive/random and metamorphic lanes |
| R11 | No known crash/UB/internal runtime failure; sanitizer/static checks | ASan/UBSan + strict compiler + full hardened corpus | **REVERIFYING** — ASan/UBSan and strict compiler gates pass; focused 4 GiB probes eliminate all five known `std::bad_alloc` failures, but full schema-4 acceptance artifact is still required |
| R12 | Fixed SAT Competition slice with SHA-256 | resolved manifest + fetch/hash check | **VERIFIED** — 30-instance SAT Competition 2023 manifest is resolved by source URL and compressed/decompressed SHA-256; acceptance fetch/hash gates pass |
| R13 | Resource-limited benchmark + raw results | schema-4 benchmark JSON | **REVERIFYING** — proof-correctness artifact exists, but final raw schema-4 artifact must show zero solver errors/crashes/verification failures under 60 s / 4 GiB |
| R14 | Honest Kissat/MiniSat comparison | pinned builds + raw results | **REVERIFYING** — pinned comparison is operational and prior raw results exist; final hardened comparison artifact is executing |
| R15 | CI mirrors critical gates | observed GitHub Actions run | **VERIFIED** — current CI includes positive and adversarial certification paths, explicit internal-error rejection, sanitizers, independent oracles, and pinned differential testing |

## Acceptance rule

R7 is not complete while any hard row is `PARTIAL`, `REVERIFYING`, `FAILED`, or `UNVERIFIED`. Actions UI color is never sufficient evidence for R11/R13/R14: the authoritative raw schema-4 acceptance JSON must have `certification.passed=true`, an empty violations list, zero SatSolv crashes, zero SatSolv solver errors, zero SatSolv verification failures, and independently validated SAT/UNSAT rows. Performance claims must be derived from that same frozen corpus and must report losses as well as wins.
