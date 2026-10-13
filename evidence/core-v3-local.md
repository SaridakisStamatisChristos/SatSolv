# Core-v3 local verification

Source lineage: exact Core v2 `b11f47060b99815620569e5fe2ad6b8071e5db01`.
Date: 2026-10-13. This records executed local evidence, not GitHub results.

- GCC 13.3, C++20, -O3, strict warnings including conversion/shadow as errors.
- 1,756 exhaustive/random formulas passed independent truth, SAT witness and RUP
  checks during implementation; a later feature matrix covers the combined policy.
- 1,632 independent model/RUP checks across four configurations and seeds 1/999.
- Pinned drat-trim `2e3b2dc0ecf938addbd779d42877b6ed69d9a985` was built locally;
  it verified the blocking14 UNSAT proof after >4,096 learned-clause deletions and
  real descriptor compaction/remapping. Exact proof-byte accounting passed.
- 300 metamorphic cases x4 transforms; nine malformed-parser cases; existing
  certification/internal-error fail-closed harness regressions passed.
- Native heap reference model, reverse reconstruction/budget boundaries, repeated
  solve reuse, seeded reproducibility/diversification, real deletion/compaction,
  and failed proof output checks passed.
- ASan/UBSan passed 272 feature/seed matrix checks plus the compaction fixture.
  Local LeakSanitizer was unavailable because this container prevents its /proc
  thread inspection. Local sanitizer runs used detect_leaks=0; GitHub retains
  detect_leaks=1. This limitation is not a leak-check pass.
- The deletion-aware independent oracle rejects an empty-clause addition once its
  sole unit justification has been deleted. This protects against validating only
  an additions projection with a stronger-than-actual proof database.
- All workflow YAML parsed locally. Remote external differential, full-corpus
  acceptance, performance ablations and proof-enabled Kissat results are pending.

No performance superiority or SOTA verdict follows from these local checks.
