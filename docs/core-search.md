# Core search experiments: ranking, retention and delayed rescue

Status: experimental, off by default. Control: the qualified structural engine at `f997df1bec87f045ffa48d16ab50729cd90e9a0e`. Base source: rejected inprocessing experiment `4162f0246209b07ce10c13c939b63323ebfd9d75`, whose preprocessing switches remain off. No merger or default adoption is authorized by the results in this document.

## Objective and implementation

The previous dynamic-LBD experiment changed ranking and critical-tier admission together. This campaign separates them and implements independent native search alternatives. The immediate implementation and its failed local screens are preserved at `f5b2428c6441a1df1b6ff1a9caf015b12e82a040`; later changes add explicit activation budgets.

| Switch | Behavior |
|---|---|
| `--experimental-dynamic-lbd` | Preserved legacy experiment, including its original current-trail score and promotion behavior |
| `--experimental-fixed-lbd-tier` | Makes critical membership depend on birth LBD, independently of legacy rank updates |
| `--experimental-rank-lbd` | Uses antecedent-level ranking with immutable birth tiers; mutually exclusive with legacy dynamic LBD |
| `--experimental-target-phases` | Preserves phases from the deepest conflict-free trail in a search window and uses them for decisions |
| `--experimental-reason-bump` | Adds deduplicated activity bumps from reasons of learned-clause variables, with a per-conflict literal-inspection budget |
| `--experimental-recent-use` | Protects ordinary clauses used in conflict analysis for one reduction round; clears the marker at each reduction |
| `--search-warmup N` | Defers selected search experiments until a conflict budget is reached; zero disables this activation condition |
| `--search-warmup-seconds S` | Alternative elapsed-time activation condition; zero disables it |

With both activation conditions zero, selected experiments start immediately. Otherwise either positive condition can activate them. The full delayed arms use one million conflicts or 15 elapsed seconds, whichever occurs first. These are explicitly tuned campaign parameters, not independently established optimal values. They were selected after observing frozen regression anchors; confirmation needs a disjoint holdout. There is no instance-name or expected-truth routing.

Modules changed: `solver.hpp`, `solver_v2.cpp`, CLI/stats serialization, native and independent proof/model tests, and dedicated qualification workflows. No external solving backend is used.

### Ranking semantics

`birth_lbd` records the number of distinct levels before backjumping. Ordinary first-UIP learning has exactly one current-level variable, its UIP. Antecedent ranking initializes to `max(1, birth_lbd - 1)` and later counts distinct non-root levels excluding the variable implied by a reason. Only reason clauses receive these updates; a first conflict clause has no identified implied variable and is skipped. This deliberately specified metric is not an assertion that adding one to every legacy analysis-time score would reproduce Glucose.

Permanent critical admission uses clause size at most two or immutable birth LBD at most two. Updating the antecedent rank never promotes an ordinary clause. At delayed activation, existing learned clauses convert once from their static birth score to the antecedent ranking convention. Root/locked reasons remain protected independently. Vivification replacements preserve birth score, rank, activity and use marker; compaction copies metadata and remaps reasons/watchers as before.

This follows the research distinction between learning-time and propagation-time LBD, but its performance is an experiment. Primary reference: [Learn to Unlearn, SAT 2025](https://cca.informatik.uni-freiburg.de/papers/GstreinPollittSchidlerFleuryBiere-SAT25.pdf).

### Target phases and bounded work

Snapshots update only the suffix that changed since the last captured trail. Every backtrack truncates the shared-prefix boundary. A conflict-free descent therefore does not repeatedly copy the entire assignment. Unknown target values fall back to ordinary saved phases. Windows reset geometrically from the configurable initial conflict interval, with saturating scheduling arithmetic. Solver reuse clears target phases, shared prefixes, epochs and activation state.

Reason-side bumps are limited to 128 inspected literals per conflict by default, deduplicated with epoch marks. Zero budget disables their work. Recency protection expires after one round unless the clause is used again; critical/root/locked handling does not depend on that marker. Clause descriptors remain 40 bytes on the tested 64-bit ABI.

The phase/reason policy is inspired by [Chasing Target Phases](https://fmv.jku.at/papers/BiereFleury-POS20.pdf). Its interaction warnings motivate independent switches rather than adopting a bundle without comparisons.

Elapsed activation is timing-sensitive: identical seeds need not produce identical post-activation trajectories on differently loaded hosts. Exact proof equivalence is checked for inactive policies; performance confirmation requires blocked paired runs and repetitions.

## Local findings, including negative results

Immediate policies were screened sequentially with no overlapping compilation/test work, seed 1, 15 seconds, 4 GiB and proof logging. These three inputs are regression diagnostics, not an independent evaluation population. Every definitive output was independently checked against the original CNF.

| Immediate arm | vmpc | clqcolor | rand_net |
|---|---|---|---|
| Qualified control | SAT 5.48 s | UNSAT 9.35 s | UNSAT 0.20 s |
| New executable, switches off | SAT 5.87 s | UNSAT 9.51 s | UNSAT 0.22 s |
| Legacy dynamic LBD | UNKNOWN | UNSAT 6.16 s | UNSAT 0.20 s |
| Legacy ranking, fixed birth tiers | UNKNOWN | UNSAT 8.16 s | UNSAT 0.21 s |
| Antecedent ranking | UNKNOWN | UNSAT 6.16 s | UNSAT 0.21 s |
| Antecedent ranking + recent use | UNKNOWN | UNSAT 5.96 s | UNSAT 0.20 s |
| Target phases | UNKNOWN | UNKNOWN | UNSAT 0.18 s |
| Reason bumping | UNKNOWN | UNSAT 8.24 s | UNSAT 0.19 s |
| Target phases + reason bumping | UNKNOWN | UNKNOWN | UNSAT 0.22 s |
| Recent use | UNKNOWN | UNSAT 6.61 s | UNSAT 0.23 s |

All eight immediate interventions miss the previous fast vmpc solve at this limit. They are unsuitable for adoption from this screen, despite some clqcolor gains. UNKNOWN at 15 seconds does not establish failure at 60 seconds. The default new executable produces byte-identical proofs and identical conflict counts to the qualified control on all three anchors: 113,926 for vmpc, 813,819 for clqcolor and 86 for rand_net. Timing differences above are single observations, not a significance claim.

The protection telemetry sharpens the causal finding. At the vmpc screen limit, legacy dynamic LBD has a peak 9,826 critical clauses versus 1,062 under its birth criterion, and 8,791 recorded tier crossings. Fixed admission reduces the peak to 1,093, yet still misses the fast solve. Antecedent ranking has no tier crossings and 1,034 protected clauses, but also misses it. Thus permanent-promotion inflation is real, while correcting admission alone is insufficient to restore the 15-second result. Ranking/search interactions remain unresolved.

Delayed rescue retains the baseline's search policy until its explicit activation budget expires, then applies the selected native mechanism. This is a new scheduling experiment, not evidence that the immediate policies have become generally superior. Its broader local screens and exact full-30 results must be evaluated separately.

## Observed verification

- Strict GCC release compilation of the final solver and semantic test executable passed.
- Native fixtures cover before/after-backjump score conventions, fixed protection, recent-use expiration, locked reasons, target prefix/backtracking and reset/reuse, zero reason budget, deduplication, epoch wrap, delayed conversion, idempotent activation and elapsed activation without sleeping.
- 3,502 independent original-CNF truth/model/RUP checks passed across 17 policy configurations and two seeds: 2,074 SAT and 1,428 UNSAT. Every UNSAT output also received independent external DRAT checking.
- 1,122 checks passed under ASan/UBSan. Local leak detection was disabled because of the execution environment; CI retains `detect_leaks=1`.
- Four full blocking14 UNSAT pressure cases passed independent external DRAT verification, each with more than 4,096 deletions and actual descriptor compaction. The combined delayed case exercises activation and subsequent target resets.
- Warmup at the maximum unsigned conflict budget produces byte-identical proofs to the new executable's baseline on the small corpus, with zero activation, phase capture, reason bump or recent-use protection.

These are scoped correctness observations, not a general solver soundness theorem, performance win or SOTA verdict. Small random tests and native fixtures are supplemented by real compaction proofs and the complete qualification matrix.

## Full GitHub qualification

`Core search qualification` compares five arms against the exact structural control, alternating order within each host across all unchanged 30 inputs: immediate antecedent ranking; delayed ranking + recent use; delayed target phases; delayed target phases + reason bumping; delayed recent use. Limits remain 60 seconds, 4 GiB, and 300 seconds for proof checking. Both source pins and real executable digests are recorded. The existing no-lost-control-solves, paired non-regression and historical numerical gates are preserved. Partial and failed runs retain evidence.

`Core search comparator benchmark` runs immediate ranking and the two principal delayed combinations against pinned MiniSat and proof-enabled Kissat 4.0.2 on all 30 inputs. MiniSat UNSAT is an uncertified performance reference. Workflow execution success is distinct from a numerical or statistical win. Current SOTA qualification would additionally require current competition/release baselines and a disjoint family-balanced holdout; it cannot be awarded from these frozen cases alone.

Rollback: leave every new switch off. Main and the qualified structural branch are unchanged. Rejected PR #7 settings remain rejected. The first-stage failed implementation is an immutable ancestor, and its measured negative results remain attached. Symmetry/parity proof-system extensions are separate future proposals, not silently enabled by this campaign.
