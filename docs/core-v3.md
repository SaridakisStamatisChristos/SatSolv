# Core v3: fixes from exact Core v2

Control: `b11f47060b99815620569e5fe2ad6b8071e5db01`.
This candidate is built directly on Core v2. Neither rejected Clause DB v2/v2.1
nor rejected restart/recursive-minimizer code is incorporated. Main stays unchanged.

## Findings and changes

| Finding | Implementation | Verification |
|---|---|---|
| Metadata access before blocker shortcut | Check the blocker first; binary watches carry direct implications without a descriptor-deletion lookup | Model/RUP matrix, differential and external DRAT |
| Historical descriptor scans | Reduce over active learned IDs; reclaim literals and descriptors together, remapping all watches, reasons and active IDs before ID reuse | Actual reductions/compaction, locked-reason invariants, sanitizer and external proof |
| Narrow simplification/minimization | Bounded root simplification, failed-literal probing, prefix vivification, indexed subsumption, non-growing bounded variable elimination, and nonrecursive binary implication minimization | Independent truth/model/RUP checks, reverse model extension, feature ablations |
| Rejected tiered retention pressure | Keep Core-v2 static-LBD retention, omit dynamic promotions and tier metadata; add recency-weighted clause activity as a separately ablatable policy | Exact control, engine-only and no-decay lanes; whole-corpus gate |
| Lazy EVSIDS heap churn | Indexed heap, at most one entry per variable, explicit score updates and linear heap rebuild on rescale | 50,000 reference-model operations, duplicate/pop/decrease/rescale checks |
| Inert seed | Deterministic SplitMix-derived tie ordering and initial phases; seed 1 retains original variable tie preference | Same-seed reproducibility and cross-seed diversification plus correctness |
| Proof-output cost and comparator asymmetry | 64-KiB buffer, explicit learned-clause deletion records, checked write/flush/close, and supplementary certified proof-enabled Kissat comparison | Byte accounting, `/dev/full`, deletion-aware independent oracle, external DRAT |

This does not implement every search mechanism in Kissat. Stable/focused modes,
rephasing and broad iterative inprocessing are not claimed. Adding these after a
failed restart experiment would require a distinct hypothesis and fresh evidence.
The implemented simplification layer addresses the concrete preprocessing gap;
whether it closes the comparative solve-count gap is a benchmark question.

## Correctness and resource contracts

- The original input is immutable. Every derived unit, strengthened clause and
  elimination resolvent is logged before being used. Preprocessing retains parent
  proof clauses; search reductions emit explicit deletions of learned clauses.
- Root units are retained in the working formula. A partial sound propagation can
  simplify using units already proved; an exhausted probing/vivification scan
  cannot justify a new clause. Interrupted BVE never commits incomplete parents
  or a partial resolvent set.
- BVE is one pass, at most eight live occurrences per polarity, no clause-count
  growth, and resolvent length at most 20. SAT assignments are reconstructed in
  reverse elimination order and independently checked against the original CNF.
- Deterministic preprocessing work budget: one million charged literal/candidate
  visits, split into root 1/4, probing 1/8, vivification 1/8, subsumption 1/4 and BVE
  1/4. Large inputs above two million literals or 200,000 variables use the original
  unsimplified engine to bound copy/workspace costs. Normalization/index building
  are linear preparation work, included in solver elapsed time.
- Binary minimization visits at most 128 edges per learned clause. The asserting
  literal is retained; support membership is updated immediately after removal;
  backtrack level and LBD are computed from the resulting clause.
- Deleted clauses cannot be reasons. Binary clauses remain undeletable. Every
  stale watch is removed before descriptor IDs are remapped/reused. Blocker hits
  may temporarily retain a harmless stale watcher until compaction.
- Static LBD <= 2 protection is unchanged from Core v2; no assertion is made that
  all possible CORE growth is bounded. The rejected dynamic-promotion mechanism
  is absent. Descriptor garbage collection is not a new retention threshold.
- Clause activity uses increment scaling with decay 0.999 and safe rescale. LBD
  still dominates ranking. `--no-clause-decay` restores cumulative activity.
- Proof-output failure produces an internal error, never a certified result.
  Solve timing includes preprocessing, model extension and final proof flushing.

## Ablations and reproducibility

Flags: `--no-simplify`, `--no-binary-minimize`, `--no-clause-decay`, `--telemetry`,
and `--reduce-interval` (default remains 2000; tests use 100 to exercise compaction).
No benchmark changes the production/default reduction interval.

The feature switches support investigation, not an undocumented runtime selector.
No rejected experiment is revived and no policy is selected after looking at one
favorable cell. Seeds other than 1 deliberately change search; frozen comparisons
continue to use seed 1.

## Evidence gates

CI keeps Release assertions enabled, runs exhaustive/random/model/RUP checks,
metamorphic and parser/harness regressions, reference heap/reconstruction tests,
ASan/UBSan/leaks, external DRAT after actual deletions and ID remapping, and pinned
MiniSat/Kissat differential tests.

The separate A/B workflow compares exact Core v2 with engine-only, full,
no-simplification, no-binary-minimization and no-clause-decay variants on the ten
frozen diagnostic instances at 15 seconds, 4 GiB and 180-second proof limits.
It additionally certifies Kissat's proofs/models in a proof-enabled comparison.
All negative results are retained; short diagnostics are not adoption evidence.

Full acceptance preserves the original 30 input hashes, 60-second solver limit,
4 GiB, 300-second proof budget, and original external-baseline commands. The
prespecified adoption target remains >=8 certified solves and PAR-2 <90.93.
Certification, this historical adoption gate and same-run comparator superiority
are reported separately. Single-run slice superiority would not establish broad
state-of-the-art performance. Do not merge before full evidence is inspected.
