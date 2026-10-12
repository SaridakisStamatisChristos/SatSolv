# Clause DB v2.1 execution record

Status: **REJECTED for adoption** after complete, independently inspected frozen
30-instance acceptance. Core v2 remains the accepted control. Do not merge PR #4.

## Local evidence before submission

- GCC C++20 -O3 strict warnings and -Werror: PASS.
- Explicit policy invariants and real SAT/UNSAT search/reduction tests: PASS.
- Existing unit assertions enabled: PASS.
- Exhaustive/random independent SAT/RUP checks: 756 formulas PASS.
- Metamorphic checks: 300 cases x4 transformations PASS.
- Malformed DIMACS: 9 cases PASS.
- Fail-closed certification and internal-error harness regressions: PASS.
- Python compilation and workflow YAML parsing: PASS.
- Generated blocking12 formula: UNSAT, 2048 conflicts, one reduction, 902
  deletions. External proof certification is a separate required remote gate.
- Local ASan/UBSan binary compiled, but LeakSanitizer could not inspect /proc in
  this execution environment. This is not sanitizer qualification; the remote
  workflow keeps leak detection enabled and requires the full sanitizer gate.

## Pins and boundaries

Core-v2 control `b11f47060b99815620569e5fe2ad6b8071e5db01` remains intact.
Rejected Clause DB v2 `ae855f9da6c6222a6ca9ec495c0b7836d21cad50` is only a reference.
Rejected Search Control v2 is not changed or combined.
Benchmark harness, competition manifest and inputs are unchanged.

The predeclared execution contract and metric limitations are documented in
`docs/clause-database-v21.md`. Focused evidence is recorded in
`evidence/clause-db-v21-focused.md`; authoritative full results and the next
bounded hypothesis are in `evidence/clause-db-v21-acceptance.md`.
