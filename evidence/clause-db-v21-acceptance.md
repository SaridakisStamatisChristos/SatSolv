# Clause DB v2.1: full acceptance and rejection decision

## Provenance and independent verification

- Candidate source: `76ecd2e2ab4648a35836961d1a93677d6c2f3ddd`.
- Exact Core-v2 control: `b11f47060b99815620569e5fe2ad6b8071e5db01`.
- Run: `38290338201`, acceptance job `114954203635`.
- Artifact: `11769877829`, `clause-db-v21-acceptance-results`.
- SHA256: `838c1c586388833909d3c222fe36c49ebfdf0704eaa9ed257b32a33fa210d1ee`.
- Full corpus: original 30 ordered instances and exact frozen hashes.
- Limits: 60-second solver budget, 4 GiB, 300-second independent proof budget.
- Original schema-4 harness and pinned MiniSat/Kissat/drat-trim are unchanged.

The downloaded archive digest matches GitHub's digest. Independent inspection
checked all 30 ordered hashes against the committed manifest, candidate commit,
resource limits, solver exit codes, external certification, complete telemetry,
tier/deletion conservation and exact proof byte accounting. PAR-2 was independently
recomputed from raw elapsed times and 120-second penalties for unsolved rows.

## Result

| Solver | Certified SatSolv solves / baseline solves | PAR-2 |
|---|---:|---:|
| Core v2 historical accepted control | 8/30 | 90.926587 |
| Clause DB v2.1 | 7/30 | 92.937689 |
| MiniSat in this full run | 8/30 | 90.459556 |
| Kissat in this full run | 14/30 | 68.558222 |

SatSolv certification passes: zero violations, crashes, solver errors or proof
verification failures. The workflow fails specifically at **Full acceptance
target**, because v2.1 misses both prespecified requirements: >=8/30 and PAR-2
<90.93. This is a performance rejection, not a SAT/UNSAT correctness failure.

The seven certified candidate solves are ibm-2004-03-k70, rbsat, c499, PRP,
ferry8, iso-ukn004 and clqcolor. All other 23 rows are UNKNOWN at the solver limit.
The harness's external `timeouts` counter is zero because SatSolv returned UNKNOWN
normally rather than being killed; it does not mean all instances were solved.

| Protected instance | Historical Core v2 | v2.1 full run |
|---|---|---|
| vmpc_28 | certified SAT, 53.770 s | UNKNOWN, 60.042 s |
| rbsat | certified SAT, 15.654 s | certified SAT, 9.960 s |
| clqcolor | certified UNSAT, 7.904 s | certified UNSAT, 7.535 s |

The positive focused search signal transfers for rbsat, but it does not restore
the historical vmpc solve. No new solve offsets that missing case.

## Decision and causal limits

**Reject v2.1 for adoption. Keep PR #4 as a draft and preserve its negative evidence.**
Core v2 remains intact and is still the strongest accepted implementation.
Rejected Clause DB v2 and Search Control v2 remain rejected and were not merged
or combined. No parameter sweep or second policy adjustment was attempted.

The 15-second screen improved 1/10 to 2/10, and reduced endpoint live clauses
29.2% versus rejected v2. This was insufficient to predict the full outcome.
The 60-second guard's contemporaneous Core-v2 arm also timed out on vmpc, so
equal UNKNOWN did not re-establish the historical solve. This limits causal
attribution: the record cannot prove that v2.1 alone caused the lost historic
outcome rather than timing variation plus policy effects. The fixed adoption gate
is nevertheless unmet and is not loosened after seeing these results.

CORE growth remains a measured concern. On full vmpc, 31,723 of 34,659 live
learnt clauses are CORE; on mchess, 28,599 of 32,523; on randxor, 25,592 of 28,581.
TIER2 caps therefore address discretionary retention but not accumulating CORE
clauses. Dynamic-LBD maintenance and CORE growth remain distinct, unisolated
explanations for residual overhead. Do not claim a fully established bottleneck.

## Next highest-leverage bounded hypothesis

Return to exact Core v2 and change the conflict-clause minimization axis, as the
handoff prescribes. Do not start from this rejected retention policy.

Candidate: **bounded, nonrecursive one-hop binary-implication minimization**.
Use the existing binary implication data to remove a non-asserting learnt literal
q when a certified binary clause implies q -> r and r is still retained in the
learnt clause. This targets literal count and proof/propagation work independently
of restart or retention tuning. It is a proposed, unexecuted experiment.

Hard constraints for that future experiment:

- preserve the asserting literal and recompute the backtrack level after removal;
- update retained-literal membership immediately, preventing circular mutual
  removals from deleting every supporting literal;
- use a deterministic, prespecified edge-visit budget, not recursive traversal;
- preserve Core-v2 retention, branching, phase, restart and proof behavior;
- instrument literals removed, successful clauses and bounded scan work;
- verify every derived UNSAT through independent RUP and external drat-trim;
- reuse the frozen corpus and limits without fitting only rbsat or clqcolor.

Do not reuse the previously rejected recursive minimizer without demonstrating
a concrete implementation or evaluation defect. This next hypothesis is narrower
and structurally different. Its benefit remains unproven until executed.
