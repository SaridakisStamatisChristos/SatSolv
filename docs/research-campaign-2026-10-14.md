# SatSolv: causal diagnosis and next qualification campaign

Date: 2026-10-14. Status: research and experiment design; proposed solver changes are not implemented or qualified by this audit.

## Decision

Keep the qualified structural engine at `f997df1bec87f045ffa48d16ab50729cd90e9a0e` as the control. Its strongest frozen-30 result is 9 certified solves, PAR-2 85.119278, at 60 seconds. Preserve rejected experiments, including `4162f0246209b07ce10c13c939b63323ebfd9d75`, with their switches off. Start with an isolated clause-retention diagnosis, then test search guidance and certified structural reasoning. Do not attempt to obtain superiority by enabling all available mechanisms together.

The evidence supports several mechanisms to investigate, not a complete explanation of every loss. No experiment in this audit establishes global superiority, and the repeatedly used frozen 30 are regression anchors rather than an untouched test set.

## Existing qualification

Completed paired workflow [38543240904](https://github.com/SaridakisStamatisChristos/SatSolv/actions/runs/38543240904) rejected all three PR #7 arms. These are within-job paired comparisons against the exact structural engine:

| Intervention | Control solves / PAR-2 | Candidate solves / PAR-2 | Lost control solve |
|---|---:|---:|---|
| Bounded elimination | 9 / 85.069864 | 8 / 89.100218 | clqcolor |
| Dynamic LBD | 9 / 85.067974 | 9 / 87.880244 | vmpc |
| Both | 9 / 85.094579 | 8 / 89.025660 | vmpc |

The separate proof-enabled comparator workflow [38543240874](https://github.com/SaridakisStamatisChristos/SatSolv/actions/runs/38543240874) completed successfully as an evaluation. Candidate scores were 8/89.129692, 8/89.941296 and 9/86.936582 respectively; Kissat 4.0.2 scored 15/66.224794, 15/65.973652 and 15/66.063364. Execution success did not mean adoption success. MiniSat's UNSAT results in this workflow had no independent proof, so they are a performance reference with a different certification contract.

All six artifacts, 270 instance records, were checked against the ordered frozen manifest, hashes, limits and expected truth; definitive SatSolv and proof-enabled Kissat outputs passed independent model/DRAT checking. The frozen manifest SHA is `7ef151bdfce7cd0344e27643446f2509823b43432a64292661a6de86ceb26a53`.

In cross-job coverage comparison, the structural control misses three SAT cases solved by Kissat (DivS, combined-crypto1, brent) and three UNSAT cases (SAT_dat, posixpath, or_randxor). This comparison identifies work targets; it is not a same-host causal experiment or a significance estimate.

## Claim ledger

| Claim | Evidence | Status and limitation |
|---|---|---|
| PR #7 settings regress under the specified gates | Completed paired full-30 artifacts | Supported for that benchmark contract; not a proof that each algorithm is intrinsically bad |
| The LBD intervention changes both ranking and permanent retention | `analyze`, `compute_lbd`, `reduce_database` in `src/solver_v2.cpp` | Supported by code; changing reuse scores can cross the protected threshold |
| The first recorded vmpc intervention is database deletion | Same executable, seed 1, flag-only binary-proof comparison | Supported: 2,000 additions identical; first differing record 2001 is a deletion in both runs |
| Retention changes explain the entire later vmpc slowdown | Prefix isolation plus full-run regression | Partially supported; exact score normalization, population growth and particular deleted clauses remain unmeasured |
| Direct small XOR recovery will solve or_randxor | Complete CNF parity-pattern scan, arity 2–6 | Unsupported: no complete groups recovered; hidden/gated parity is unresolved |
| New search guidance can improve our SAT misses | Missing target/best phases in this engine; primary research | Plausible hypothesis, no SatSolv ablation yet |
| Symmetry reasoning is a relevant contemporary capability | 2026 official results and winning solver description | Supported as prior art; usefulness on our six misses is unresolved |
| We can now claim current SOTA | Existing scores and narrow benchmark | Contradicted by our current comparator results; current competition solvers have not been evaluated here |

The two completed full-30 workflows are distinct evaluations of the same candidate, not independent replications of an untouched test. The paper, solver description, and repository for a technique are related provenance, not three independent empirical confirmations.

## New causal finding: mixed LBD measurement and retention

At creation, `compute_lbd(learnt)` runs before `cancel_until(backtrack)`. During later conflict analysis, dynamic LBD counts the current distinct non-root levels and decreases the stored score. `reduce_database` permanently excludes `c.lbd <= 2` from deletion. Creation and later reason-trail scores therefore have different contexts: backjump/assertion can merge the asserting literal's level into an existing level. A 3-to-2 update can change tier admission as well as ranking.

The primary literature explicitly distinguishes pre-backjump LBD from propagation-time glue and describes different reconciliation choices [R1]. This is a heuristic semantics/confounding issue, not a demonstrated unsoundness bug. Our update also runs on the first conflict clause as well as later reason clauses; blindly adding one to every reuse score would not establish consistent semantics.

The new two-second vmpc diagnostic uses the same `satsolv-search-final` executable from source `4162f024...`, seed 1, with only dynamic LBD toggled. All first 2,000 proof additions match. Both traces first delete at record 2000, then differ at record 2001 (zero-based). This locates the first recorded divergence at the first reduction. It does not prove that only permanent promotion, rather than ranking, causes the full-run loss. Partial timeout traces are not complete UNSAT certificates. See `evidence/causal-audit-2026-10-14/prefix-result.json`.

## New structural and competitor diagnostics

Complete parity-pattern recovery groups clauses by unsigned variable set, collects forbidden assignments, and accepts an XOR only when every assignment of one parity is present. Arity 2–6 results:

| Case | Width profile | Complete XOR equations |
|---|---|---:|
| clqcolor | 1,344 ternary; 168 binary; 15 wider | 0 |
| vmpc | 65,604 ternary; 42,420 binary; 56 width-28 | 0 |
| posixpath | 4,264 binary; 27 width-13 | 0 |
| or_randxor | Mixed width 3–6 | 0 |
| rand_net | 10,920 binary; 5,740 ternary; one width-70 | 70 binary equations |

These are original-CNF scans. They neither exclude parity behind auxiliary/gating encodings nor prove that binary clauses form cardinality constraints. In particular clqcolor is predominantly ternary, so it should not be described as a mostly-binary formula. See `structural-shapes.json`.

We also ran sequential, single-seed, proof-enabled Kissat 4.0.2 diagnostics, pinned at `f7e57764c19cf35198830575492dc9d80e872b55`, seed 1, 10 seconds, 4 GiB, 300-second verification allowance. No compilation or other solver run overlapped these runs. Wall time below is a local observation, not adoption evidence:

| Removed mechanism | posixpath | or_randxor |
|---|---:|---:|
| None | UNKNOWN at 10 s | UNSAT 1.36 s |
| Elimination | UNSAT 5.36 s | UNSAT 1.61 s |
| Vivification | UNSAT 5.34 s | UNSAT 1.28 s |
| Stable mode | UNSAT 4.74 s | UNSAT 1.53 s |
| Target phases | UNSAT 4.54 s | UNSAT 1.31 s |
| Rephasing | UNSAT 6.30 s | UNSAT 1.08 s |
| Congruence | UNKNOWN at 10 s | UNSAT 1.24 s |

All 12 definitive UNSAT outputs independently passed DRAT checking. The two UNKNOWN results are censoring, not proof failures. This seed differs from the comparator's default Kissat seed, and the local limit is shorter. Every individual removal still solves or_randxor; none is individually necessary in this diagnostic. Dependencies and redundant combinations remain possible. Performance effects inside Kissat cannot be transplanted causally into SatSolv. See `competitor-mechanisms.json` for exact commands, hashes and verification records.

## Ordered implementation specifications

### PR A: measurable and separable clause retention

Objective: determine whether the rejected LBD result was caused by dynamic ranking, critical-tier admission, or their combination. Why now: the first divergence is deletion, and the current experiment confounds both decisions.

Scope: separate immutable birth score, consistently defined reuse score, and retention class. Record creation/reuse distributions, 3-to-2 promotions, protected live clauses/literals, eligible deletions, reason locks, and recent conflict use. Keep the structural control behavior exactly reproducible when the experiment is off. Preserve descriptor compaction and proof deletion invariants.

Modules: `solver.hpp`, `solver_v2.cpp`, stats serialization, existing proof/feature tests. Non-goals: changing restart, branching, simplification or minimization.

Experiments: unchanged control; legacy PR #7 intervention; dynamic ranking with birth-tier membership fixed; normalized reuse metric with fixed membership; then separately bounded promotion/recent-use protection. Define whether a score is measured at learning, propagation or analysis before implementing normalization. Include a fixture with levels `(1,2,3)` before backjump and `(1,2,2)` after assertion. Do not select expected SAT/UNSAT or instance names at runtime.

Acceptance: source-level semantics fixtures; independent truth/model/proof feature checks; sanitizer and real-compaction checks; no altered control trajectory; complete paired frozen-30 gate. Reject if vmpc remains lost, if overhead erases gains, or if the full non-regression gate fails. Population telemetry must confirm or falsify the retention-pressure explanation before describing it as the root cause. Rollback: switches off; archive the measured intervention.

### PR B: model-directed search with controlled coupling

Objective: address SAT coverage using a preserved promising partial assignment instead of relying only on the most recent phase. Scope: best/target phases, bounded rephasing and separately selectable reason-side variable bumping. Start with unchanged restart scheduling. Only after those ablations test coupled focused/stable scheduling as a new experiment with a defined interaction hypothesis; do not silently revive the rejected restart branch.

Modules: phase arrays, enqueue/backtrack/branch selection, conflict analysis, statistics. Non-goals: expected-truth routing, external solving portfolios, name-dependent seeds. Heuristic choices must preserve completeness, model validation, proof logging and bounded interruption behavior.

Evidence: target-phase research also reports harmful interactions from naive mode alternation [R2]. It is a design guide rather than a guarantee. Tests: deterministic seed behavior, reset/reuse, empty and partial assignments, original-model checking, proof checks, sanitizer. Gate each isolated arm, then useful combinations, against PR A's qualified winner or the structural control. Reject broad UNSAT losses or merely shifting a near-timeout solve. Rollback: saved-phase policy remains selectable.

### PR C: use-aware clause lifetime

Objective: test recent conflict use and finite protection independently of ranking. Dependency: PR A's defined score semantics and population telemetry. Scope: one bounded second-chance/age policy and live-literal budget; distinguish binary, root reason, locked, and ordinary learned clauses. No arbitrary expanding tier thresholds. Expire stale optional protection safely; never delete a locked reason.

Modules: clause metadata, analyze/reduce/compaction, stats/proof deletion. Tests: protected/used/unused boundary states, epoch wrap, reason remapping and real reduction pressure. Acceptance: independently certified results, no lost control solves and lower paired PAR-2; report memory, propagation and proof cost. Reject a policy that improves clqcolor but loses vmpc. Rollback: fixed birth-tier policy.

### PR D: measured structure-preserving inprocessing

Objective: simplify enough to expose structure without repeating bulk BVE's clqcolor loss. Scope: verified gate/cardinality recognition, bounded subsumption/self-subsumption, and selective learned-clause vivification with per-pass yield and cost. Every proposed rewrite needs a checked proof step and model restoration. The already qualified congruence implementation is the starting point.

Modules: congruence, simplifier/eliminate, watcher/root propagation and restoration. Non-goals: enabling every PR #7 flag, thresholds chosen from filenames, treating fewer variables as proof of better search. Tests: incomplete encodings, signed cycles, interruption at every mutation boundary, independently checked original models/DRAT and conflict pressure. Acceptance: simplify/search time breakdown, actual reductions, frozen gate plus family holdout. Reject zero-yield effort or a clqcolor regression. BVE remains off pending an identified scheduling/structure defect; it is not automatically rehabilitated by new caps.

### PR E: certified symmetry capability spike

Objective: determine whether our misses contain exploitable symmetry. Why relevant: the official 2026 Main winner is satsuma-iter+kissat [R3], whose description uses iterative lightweight symmetry breaking with SR certificates [R4]. This is evidence of a contemporary capability, not evidence that it explains our frozen losses.

First deliverable: bounded signed incidence-graph detection and independently validated generators; report orbits, affected clauses, overhead and no-symmetry fallback. Validate each proposed permutation against the full original formula before reasoning from it. Lack of a simple variable transposition does not exclude a simultaneous row/column permutation.

Second deliverable only if structure is recovered: native bounded fixing/cuts with an explicit proof contract. Symmetry breakers need only preserve satisfiability and are not generally RUP consequences. Do not append SR records to a DRAT file or assume our small RUP oracle validates RAT/SR reasoning. Either produce proofs within the accepted DRAT contract, or prepare a distinct end-to-end certified SR research lane with a pinned compatible checker and documented composition with the CDCL proof. Existing DRAT qualification remains intact. Small truth enumeration, adversarial false generators, original model checking, independent UNSAT certification and interruption tests are mandatory.

Gate: net solver-plus-preprocessing-and-checking benefit on recovered symmetry families, low overhead on unrelated families, then complete qualification. If no useful symmetry is recovered, stop the production integration and retain the diagnostic. Rollback: detection off. This is a proof-capability change, not a cosmetic preprocessing flag.

### PR F: parity/extension reasoning after recovery

Objective: handle genuinely recovered algebraic structure or repeated learned-clause factors. Dependency: demonstrable recoverable structure and a proof/mapping design. First recover gates, conditional parity or factors with exact defining-clause witnesses; quantify equations, rank and useful consequences. Only then implement a bounded algebraic/extension engine.

Primary work already demonstrates certified Gauss-Jordan reasoning [R5] and learned-clause factoring [R6]; these are prior art, not our novelty. DRAT can express more than our present RUP-only oracle. New variables require capacity, model-extension and proof validation changes. Do not accept an algebraic contradiction without an independently checked original-CNF certificate. Gate net costs and family-general benefit; reject the spike if extraction finds nothing useful or proof costs dominate. Rollback: ordinary CDCL plus qualified congruence.

## Qualification ladder and stopping rules

1. Correctness first: independently validated original-CNF SAT models and UNSAT certificates; reset/reuse, limits, parser/API boundaries, signed substitutions and extension reconstruction; sanitizer and actual reduction/compaction stress. UNKNOWN remains UNKNOWN. Fail closed on invalid output or checker timeout.
2. Cheap diagnostic screens: fixed source, executable/input SHA, predeclared seeds, no simultaneous build/test load, normalized work counters. These can reject bad hypotheses but cannot adopt defaults.
3. Frozen regression gate: exact control/candidate commits, all 30 original hashes, alternating/randomized within-host paired arm order, 60 seconds, 4 GiB, 300-second proof checking. Retain no-lost-solves and paired PAR-2 non-regression. A historical scalar cutoff supplements rather than replaces the paired control. Preserve logs, commands, raw outputs, exit codes and proof verification.
4. Independent confirmation: freeze a disjoint family-balanced holdout before tuning; allocate separate tuning and confirmation families; use seed 1 and predeclared seeds 2–5, three blocked repetitions where resources permit. Do not count repetitions as new independent instances. Report censored PAR-2, SAT/UNSAT and family results, paired differences, uncertainty using instances/families as sampling units, proof bytes, peak RSS and solver/checker time separately and end-to-end. Predeclare the longer-budget lane (e.g. 300 seconds) separately from the 60-second contract.
5. Comparator ladder: keep Kissat 4.0.2 for historical continuity; add a new pinned 4.0.4/current-competition lane. The 2026 winner and SAT-specialized leading systems are relevant references [R3, R4]. Use their actual competition sources and checker contracts, budget all preprocessing, and preserve provenance/licenses. A wrapper around another solver does not count as a native SatSolv core win.
6. Adoption requires all correctness gates, the frozen gate, holdout non-regression and meaningful comparator improvement under matched hardware/resources/certification. Calling the engine generally superior requires broader current-baseline evidence; one new frozen solve is not that evidence. A failed stage remains archived and off. Never recursively stack a rejected change onto another rejected change.

Successful scope is measurable certified improvement and ultimately a clearly specified benchmark win. Universal fixed-time superiority on arbitrary CNFs is not something this campaign can promise. Main is unchanged; proposed production defaults await qualification.

## Evidence and primary references

Repository evidence: [PR #6](https://github.com/SaridakisStamatisChristos/SatSolv/pull/6), [rejected PR #7](https://github.com/SaridakisStamatisChristos/SatSolv/pull/7), their pinned commits and linked complete Actions runs. Diagnostic JSON files live beside this report under `docs/evidence/causal-audit-2026-10-14/`. Local timeout proof prefixes support sequence comparison, not full proof certification.

- R1: Gstrein et al., [Learn to Unlearn](https://cca.informatik.uni-freiburg.de/papers/GstreinPollittSchidlerFleuryBiere-SAT25.pdf), SAT 2025, DOI 10.4230/LIPIcs.SAT.2025.14; score conventions, separate ranking/protection experiments and recent conflict use.
- R2: Biere and Fleury, [Chasing Target Phases](https://fmv.jku.at/papers/BiereFleury-POS20.pdf), POS 2020; phase guidance and heuristic interactions.
- R3: [Official SAT Competition 2026 results](https://satcompetition.github.io/2026/downloads/satcomp26slides.pdf) and [updated competition website](https://satcompetition.github.io/2026/), including August corrections. Results are competition-specific, not measurements of SatSolv.
- R4: Anders and Codel, [Iterative Symmetry Handling in the SAT Competition 2026](https://markusanders.eu/publications/satsuma_sc26.pdf); [Satsuma repository](https://github.com/markusa4/satsuma); [Orbitopal Fixing in SAT](https://arxiv.org/abs/2601.16855). Symmetry certification/composition must be evaluated explicitly.
- R5: Soos and Bryant, [Proof Generation for CDCL Solvers Using Gauss-Jordan Elimination](https://arxiv.org/abs/2304.04292), proof-producing algebraic reasoning.
- R6: Pollitt et al., [Factoring Learned Clauses](https://cca.informatik.uni-freiburg.de/papers/PollittBattlemanFleuryVizelHeuleBiereBryant-SAT26.pdf), SAT 2026, DOI 10.4230/LIPIcs.SAT.2026.28.
- R7: [Kissat release 4.0.4](https://github.com/arminbiere/kissat/releases/tag/rel-4.0.4). Historical controls must not be silently replaced by a new version.
