# External structural-mechanism validation

Status: **completed; v1 INVALID_EVIDENCE and insufficient control coverage. No SatSolv implementation authorized.**

## Prior evidence

The frozen 2023 necessity-attribution campaign identified three narrow Kissat 4.0.4 mechanisms that crossed its pre-registered threshold:

- probing: 4/6 material degradations when disabled;
- gate congruence: 2/6 material degradations;
- bounded variable addition / factoring: 2/6 material degradations.

Plain BVE and vivification produced 0/6 material degradations. The earlier external XOR-congruence test also produced 12/12 neutral cases. Those failed mechanisms remain rejected from their current hypotheses.

The 2023 diagnostic cases were used to discover these candidates and therefore cannot validate them. This campaign used a disjoint external holdout before any SatSolv solver code was added.

## Holdout construction

Source: SAT Competition 2026 selected-benchmark metadata pinned to `satcompetition/2026@3e2dee7cf5aa643c77bf946280a3c1c520756ef9`.

The holdout contains **18 instances from six untouched families, exactly three instances per family**:

- graph-coloring;
- argumentation;
- van-der-Waerden;
- ECC equivalence checking;
- stable semantics;
- count.

The truth mix is deliberately balanced: **9 SAT and 9 UNSAT**.

Excluded before execution:

- all three families used in the earlier XOR holdout (`xor-shifting`, `lights-out`, `syndrome-decoding`);
- every frozen 2023 diagnostic instance used to discover the structural candidates.

Exact GBD content identifiers, isohash2 identities, family labels and truth labels are committed in `benchmarks/structural_holdout_2026.json` before execution.

## Causal arms

One pinned Kissat 4.0.4 executable (`8af8e56f174b778aef3aa45af9f739b2a5f492c2`) was compared under four arms:

| Arm | Option | Hypothesis |
|---|---|---|
| `control` | default | reference |
| `no_probe` | `--probe=0` | probing orchestration |
| `no_congruence` | `--congruence=0` | gate congruence |
| `no_factor` | `--factor=0` | bounded variable addition / factoring |

Every definitive SAT result required independent model checking. Every definitive UNSAT result required an external DRAT verdict from pinned `drat-trim`. The exact same Kissat executable, seed, cutoff and memory budget were used within a case.

Limits:

- seed 1;
- 60 seconds per solver arm;
- 4 GiB memory;
- 300 seconds per proof check.

Arm order was rotated deterministically by holdout index; it was never adapted after observing performance.

## Pre-registered external-validation rule

Material degradation means either:

1. control solves but the ablated arm does not; or
2. both solve, but the ablated arm is at least **2.0x** slower.

Material reversal is defined symmetrically.

The holdout was considered sufficiently informative only if default Kissat solved at least **10 of 18** cases within the fixed budget.

A mechanism could pass external validation only if all of the following were true:

- at least **4 material degradations**;
- those degradations span at least **3 families**;
- degradations outnumber material reversals;
- the global control-coverage threshold is met;
- all evidence-integrity checks pass.

## Observed result — 2026-10-15

GitHub Actions run `38678059945` completed all 18 cases and the aggregate gate.

Aggregate verdict: **`INVALID_EVIDENCE`**.

- cases completed: **18 / 18**;
- certified default-control solves: **8 / 18**;
- required control solves: **10 / 18**;
- sufficient control coverage: **false**;
- externally validated candidates: **none**;
- integrity errors: `stable-semantics-03: per-case integrity failed`.

The eight certified control solves were:

- `argumentation-01`, `argumentation-02`, `argumentation-03`;
- `ecc-equivalence-01`, `ecc-equivalence-02`, `ecc-equivalence-03`;
- `stable-semantics-01`, `stable-semantics-02`.

The three graph-coloring cases, all three count cases, and all three van-der-Waerden cases were uninformative at this cutoff because default Kissat did not produce a certified solve. Thus the intended six-family design collapsed to useful evidence from only three families.

### Certification failure on `stable-semantics-03`

Pinned Kissat default returned UNSAT in about 24.5 seconds, but its proof did **not** receive the required external verification within the fixed 300-second proof budget. Two ablated arms returned externally verified UNSAT results. The harness therefore marked the case integrity-failed.

Any apparent `no_probe` or `no_factor` material reversals on this case are **not valid performance evidence**, because the default control was not independently certified under the common contract. They must not be counted downstream.

### Mechanism result under the valid evidence subset

No candidate mechanism produced a valid material degradation on the usable cases:

- probing: **0 valid material degradations**;
- gate congruence: **0 valid material degradations**;
- factoring/BVA: **0 valid material degradations**.

This does not refute the frozen-2023 diagnostic signals, because v1 lacked the pre-registered minimum control coverage and had an integrity failure. It does refute using this particular 18-case set as external validation.

## Decision

**No SatSolv structural implementation is authorized from v1.**

Do not lower the 10/18 coverage gate, increase the materiality threshold post hoc, reinterpret unverified UNSAT as certified, or count the invalid `stable-semantics-03` reversals.

The next campaign must repair **holdout construction**, not solver code. It will use a separate **control-only qualification screen** over untouched SAT Competition 2026 families. Candidate selection will depend only on default pinned Kissat producing an independently certified result with substantial runtime margin; no ablation outcome will be observed during selection. The resulting selected set must then be frozen in a new commit before `no_probe`, `no_congruence`, or `no_factor` are executed.

## What a future PASS authorizes

A future clean PASS still does **not** merge or enable anything in SatSolv. It only makes the mechanism eligible for a dedicated implementation experiment.

The implementation phase must still:

1. identify the smallest proof-preserving mechanism rather than copy a broad Kissat subsystem;
2. preserve model reconstruction and certified UNSAT;
3. compare the exact candidate against the validated target-phase SatSolv control;
4. use independent correctness checking and same-binary or source-controlled causal evidence;
5. reject the implementation if it loses control solves or fails its own pre-registered gate.

Generic root probing remains separately rejected by the earlier SatSolv experiment. A future `no_probe` holdout PASS therefore means the **Kissat probing pipeline contains important work**, not that SatSolv should re-enable its old root-probe implementation.

Factoring/BVA is especially constrained: it can introduce variables and therefore requires an explicit proof/model-extension design before implementation. No auxiliary-variable/RAT expansion is silently authorized.
