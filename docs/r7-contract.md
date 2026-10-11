# R7 requirement ledger

This ledger is intentionally evidence-stateful. Presence of code is not verification.

| ID | Hard requirement | Evidence required | Current state before CI |
|---|---|---|---|
| R1 | Native CDCL, no solver wrapping | source inspection + unit/exhaustive tests | PARTIAL |
| R2 | Robust DIMACS parser | unit + adversarial parser tests | VERIFIED locally |
| R3 | Two-watch BCP | unit + exhaustive/random/metamorphic | VERIFIED locally |
| R4 | First-UIP + backjump + learning | conflict tests + exhaustive/random | VERIFIED locally |
| R5 | EVSIDS/phase/restarts/reduction/LBD | source + exercised stats/tests | PARTIAL |
| R6 | SAT witness soundness | independent model oracle | VERIFIED locally |
| R7 | DRAT for every certified UNSAT | proof files + checker | PARTIAL |
| R8 | drat-trim accepts UNSAT proofs | external drat-trim logs | UNVERIFIED until CI |
| R9 | Differential consistency | MiniSat + Kissat differential run | UNVERIFIED until CI |
| R10 | Exhaustive/property checks | brute-force + metamorphic logs | VERIFIED locally |
| R11 | Sanitizer/static checks | ASan/UBSan + strict compiler | PARTIAL |
| R12 | Fixed SAT Competition slice with SHA-256 | resolved manifest | UNVERIFIED |
| R13 | Resource-limited benchmark + raw results | benchmark JSON | UNVERIFIED for competition slice |
| R14 | Honest Kissat/MiniSat comparison | pinned builds + raw results | UNVERIFIED for competition slice |
| R15 | CI mirrors critical gates | observed GitHub Actions run | UNVERIFIED until push |

Local verification is specific to the pre-push working tree. Final status must be updated from the pushed commit and CI evidence.
