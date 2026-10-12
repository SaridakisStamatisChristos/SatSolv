# Clause DB v2.1: independently inspected focused evidence

Candidate source: `76ecd2e2ab4648a35836961d1a93677d6c2f3ddd`.
Exact control: `b11f47060b99815620569e5fe2ad6b8071e5db01`.
Rejected-v2 reference: `ae855f9da6c6222a6ca9ec495c0b7836d21cad50`.
Workflow run: `38290338201`.

## Qualification

Remote strict builds, explicit policy tests, enabled Release unit assertions,
756 exhaustive/random independent SAT/RUP checks, 1,200 metamorphic transforms,
malformed-input cases, fail-closed harness checks, SAT witnesses, external DRAT,
ASan/UBSan with leak detection, and MiniSat/Kissat differential checks all passed.
The ordinary PR CI run `38290342678` also passed.

Certification artifact: `11766145005`.
Digest: `ece0658f00ad48beccb24648399c3e03848f5bffe653be68b00cd0525bd566ba`.
The raw artifact includes the blocking12 CNF, proof, statistics, solver transcripts
and drat-trim transcript. It demonstrates 2048 conflicts, 2047 generated learnt
clauses, one database reduction, 902 deletions and a 15,360-byte binary DRAT proof.
Pinned external drat-trim reports `s VERIFIED` after 15,360 resolution steps.

## Frozen diagnostic screen

Limits remain 15 s, 4 GiB and 180 s independent proof verification. All three
variants used identical ordered input hashes and the unchanged schema-4 harness.
Every result artifact has certification passed with zero violations, crashes,
solver errors and verification failures. Candidate telemetry is complete for all
ten rows; independently reconstructed tier/deletion accounting is consistent.

| Metric | Core v2 | v2.1 | rejected v2 |
|---|---:|---:|---:|
| Certified solves | 1/10 | 2/10 | 1/10 |
| PAR-2 | 27.958862 | 25.620542 | 27.517520 |
| Conflicts/s | 33,444.90 | 25,912.32 | 19,285.46 |
| Propagations/s | 3,505,191.48 | 3,246,314.83 | 2,583,156.10 |
| Generated learnt clauses | 4,835,770 | 3,528,858 | 2,703,260 |
| Deleted learnt clauses | 4,800,390 | 3,431,356 | 2,565,519 |
| Endpoint live learnt clauses (sum) | 35,380 | 97,502 | 137,741 |
| Proof bytes (sum) | 390,306,334 | 336,967,409 | 284,087,700 |

v2.1 solves rbsat in 9.331 s (control UNKNOWN at 15 s), and clqcolor UNSAT in
6.874 s versus control 9.589 s. Its clqcolor proof requires 481,935 conflicts
versus control 722,440. The rejected v2 remains faster on clqcolor alone at 5.175 s.

The predeclared screen gate passes. The candidate has approximately 29.2% fewer
endpoint live clauses than v2 and 34.4% higher aggregate conflict throughput,
but 23% lower conflict throughput than Core v2. Propagation throughput is 25.7%
higher than v2 and approximately 7.4% lower than Core v2.

These throughput ratios compare evolving searches, not identical operation traces.
They are observed work rates, not isolated measurements of BCP implementation cost.

## 60-second protected-instance guard

Limits: 60 s, 4 GiB, 300 s external proof verification; same ordered hashes.

| Instance | Core v2 | v2.1 |
|---|---|---|
| vmpc_28 | UNKNOWN at 60.040 s | UNKNOWN at 60.038 s |
| rbsat-v760c43649gyes10 | certified SAT, 21.298 s | certified SAT, 9.350 s |
| clqcolor | certified UNSAT, 9.620 s | certified UNSAT, 6.872 s |

Both arms have 2/3 solves and zero certification violations. PAR-2: 50.306142
control versus 45.407439 candidate. The numeric non-regression gate passes.

**The historical vmpc solve is not re-established.** The inspected historical
Core-v2 full artifact (`11757437290`, run `38264087016`) demonstrates vmpc SAT at
53.770 s, rbsat SAT at 15.654 s and clqcolor UNSAT at 7.904 s. Solver-source and
CMake files are unchanged between that historic run and the pinned control.
The contemporary control timing differs, so the guard cannot establish that
v2.1 preserves the historic vmpc outcome. Do not treat equal UNKNOWN as that proof.

## Mechanism evidence and remaining uncertainty

Candidate screen endpoint totals: CORE 67,266; TIER2 4,625; LOCAL 25,611.
Promotions: 382,814; demotions: 285,812; formerly-TIER2 clauses reused before
eventual deletion: 61,658. No CORE clauses were deleted.

The candidate's live pool remains 2.76 times Core v2's aggregate endpoint pool.
On mchess, 88.8% of retained clauses are CORE and live count is 3.97 times control.
On randxor, 80.4% are CORE and live count is 6.58 times control. On the 60 s vmpc
row, 32,844 of 36,197 retained clauses are CORE, while the control retains 5,758
clauses overall. Bounded TIER2 retention therefore does not bound CORE growth.

Reduced discretionary retention plausibly explains improvement versus v2.
Residual CORE growth and dynamic-LBD maintenance are competing explanations for
remaining overhead versus Core v2. This evidence does not isolate their costs.
No threshold tuning or additional search mechanism was introduced after results.

## Artifact integrity and full-corpus boundary

Focused artifact: `11767245969`.
Digest: `dcba0fcb689cc25726d9788dfbb7265e1c45bf49d89e5673bdb2b0d651dd34ec`.
Both downloaded archive SHA256 values match GitHub's artifact digests.

Full frozen acceptance advanced automatically after the numeric gates. It uses
the original 30-instance manifest, 60-second solver limits, 4 GiB, 300-second
proof limits and pinned MiniSat/Kissat. Full-corpus results and adoption decision
must be recorded separately after raw inspection. Focused passes are not merge
authorization, whole-corpus superiority, or proof of vmpc preservation.
