# External structural-mechanism validation

Status: **pre-registered SAT Competition 2026 holdout; execution required before any SatSolv implementation**.

## Prior evidence

The frozen 2023 necessity-attribution campaign identified three narrow Kissat 4.0.4 mechanisms that crossed its pre-registered threshold:

- probing: 4/6 material degradations when disabled;
- gate congruence: 2/6 material degradations;
- bounded variable addition / factoring: 2/6 material degradations.

Plain BVE and vivification produced 0/6 material degradations. The earlier external XOR-congruence test also produced 12/12 neutral cases. Those failed mechanisms remain rejected from their current hypotheses.

The 2023 diagnostic cases were used to discover these candidates and therefore cannot validate them. This campaign uses a disjoint external holdout before any SatSolv solver code is added.

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

One pinned Kissat 4.0.4 executable (`8af8e56f174b778aef3aa45af9f739b2a5f492c2`) is compared under four arms:

| Arm | Option | Hypothesis |
|---|---|---|
| `control` | default | reference |
| `no_probe` | `--probe=0` | probing orchestration |
| `no_congruence` | `--congruence=0` | gate congruence |
| `no_factor` | `--factor=0` | bounded variable addition / factoring |

Every definitive SAT result requires independent model checking. Every definitive UNSAT result requires an external DRAT verdict from pinned `drat-trim`. The exact same Kissat executable, seed, cutoff and memory budget are used within a case.

Limits:

- seed 1;
- 60 seconds per solver arm;
- 4 GiB memory;
- 300 seconds per proof check.

Arm order is rotated deterministically by holdout index; it is never adapted after observing performance.

## Pre-registered external-validation rule

Material degradation means either:

1. control solves but the ablated arm does not; or
2. both solve, but the ablated arm is at least **2.0x** slower.

Material reversal is defined symmetrically.

The holdout is considered sufficiently informative only if default Kissat solves at least **10 of 18** cases within the fixed budget.

A mechanism passes external validation only if all of the following are true:

- at least **4 material degradations**;
- those degradations span at least **3 families**;
- degradations outnumber material reversals;
- the global control-coverage threshold is met;
- all evidence-integrity checks pass.

Possible aggregate decisions:

- `IMPLEMENTATION_EXPERIMENT_ELIGIBLE` — one or more mechanisms pass;
- `NO_EXTERNAL_VALIDATION` — coverage is sufficient but no mechanism passes;
- `INSUFFICIENT_CONTROL_COVERAGE` — the chosen holdout is not informative enough under 60 seconds;
- `INVALID_EVIDENCE` — certification or integrity failed.

## What a PASS authorizes

A PASS does **not** merge or enable anything in SatSolv. It only makes the mechanism eligible for a dedicated implementation experiment.

The implementation phase must still:

1. identify the smallest proof-preserving mechanism rather than copy a broad Kissat subsystem;
2. preserve model reconstruction and certified UNSAT;
3. compare the exact candidate against the validated target-phase SatSolv control;
4. use independent correctness checking and same-binary or source-controlled causal evidence;
5. reject the implementation if it loses control solves or fails its own pre-registered gate.

Generic root probing remains separately rejected by the earlier SatSolv experiment. A `no_probe` holdout PASS therefore means the **Kissat probing pipeline contains important work**, not that SatSolv should re-enable its old root-probe implementation.

Factoring/BVA is especially constrained: it can introduce variables and therefore requires an explicit proof/model-extension design before implementation. No auxiliary-variable/RAT expansion is silently authorized here.
