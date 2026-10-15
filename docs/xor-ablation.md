# XOR congruence mechanism attribution

Status: **completed; NO-GO for a SatSolv XOR-congruence implementation from this hypothesis**.

Base candidate: `exp/satsolv-deterministic-target` at `49b31d39a418d469802a7143b0c3c4e070f24d37`.

## Why this experiment exists

The validated target-phase candidate closes one certified solve on the frozen 30-instance development corpus, but pinned Kissat 4.0.4 still solves five more instances. One conspicuous remaining loss is `or_randxor_k3_n520_m520.cnf`. A filename is not causal evidence, so SatSolv must not add Gaussian elimination or XOR-specific machinery merely because the instance name contains `xor`.

Pinned Kissat 4.0.4 (`8af8e56f174b778aef3aa45af9f739b2a5f492c2`) exposes XOR gate extraction inside congruence closure as the independent option `congruencexors`, enabled by default. That gives a direct falsification experiment: compare the **same Kissat executable** with default settings versus `--congruencexors=0`.

## External holdout

The performance decision does not use the 2023 `or_randxor` development instance. It uses 12 selected SAT Competition 2026 instances pinned by GBD content identifiers and by the SAT Competition metadata repository commit `3e2dee7cf5aa643c77bf946280a3c1c520756ef9`:

- 4 `xor-shifting` instances;
- 4 `lights-out` instances, balanced 2 SAT / 2 UNSAT;
- 4 `syndrome-decoding` SAT instances.

These instances were not part of SatSolv's frozen 2023 development corpus. The exact identities and expected-result labels are committed in `benchmarks/xor_ablation_2026.json` before execution.

`UNKNOWN` in the competition metadata is not promoted to a truth label. If either arm produces SAT or UNSAT on such an instance, the result must still pass independent model or DRAT verification before it counts as solved.

## Causal contract

Per instance:

1. download the fixed GBD object and record both source and decompressed CNF SHA-256 digests;
2. build pinned Kissat once;
3. run **control** with no XOR option override;
4. run **no_xor** with exactly `--congruencexors=0`;
5. use the same seed, binary, memory budget and cutoff for both arms;
6. verify SAT models independently and verify UNSAT proofs with pinned `drat-trim`;
7. classify the effect only after certification.

A material degradation is either:

- control solves while `no_xor` does not; or
- both solve, but `no_xor` takes at least 2.0x the control runtime.

A material reverse is defined symmetrically.

## Pre-registered decision rule

The XOR mechanism is **SUPPORTED** only if all of the following hold on the 12-instance holdout:

- at least 3 material degradations;
- degradations span at least 2 benchmark families;
- at least one degradation occurs in `xor-shifting`;
- material degradations outnumber material reversals;
- every available definitive SAT/UNSAT result is independently certified and all causal-integrity checks pass.

If the rule passes, the next authorized experiment is a **bounded proof-producing XOR-gate congruence module** inside SatSolv, reusing the existing congruence/proof/model-restoration infrastructure. This does **not** authorize Gaussian elimination.

If the rule fails, XOR congruence is a NO-GO from this hypothesis. Preserve the negative evidence and continue feature attribution on the remaining Kissat-only wins instead of forcing an XOR implementation.

## Observed result — 2026-10-15

GitHub Actions run `38676477041` completed all twelve same-binary causal comparisons and the aggregate integrity check.

Observed aggregate result:

- causal/integrity gate: **PASS**;
- cases completed: **12 / 12**;
- material degradations after disabling `congruencexors`: **0**;
- material reversals: **0**;
- neutral cases: **12**;
- degradation families: **0**;
- `xor-shifting` degradations: **0**;
- pre-registered mechanism decision: **`NO_GO_XOR_CONGRUENCE`**.

All four `xor-shifting`, all four `lights-out`, and all four `syndrome-decoding` cases were neutral under the pre-registered 2x/control-only-solve criterion. The result is stronger than merely missing the threshold: there was no material directional effect in either direction on the complete holdout.

This falsifies the current hypothesis that Kissat 4.0.4's XOR-gate extraction is a material explanation for SatSolv's observed parity-oriented performance gap under this 60-second external holdout. It does **not** prove XOR reasoning is never useful, and it does not identify the cause of `or_randxor`; it says this specific mechanism is not justified as SatSolv's next intervention by the evidence gathered here.

### Decision

Do **not** implement XOR congruence from this hypothesis. Do **not** add Gaussian elimination. Preserve the experiment as a negative result and move to orthogonal feature attribution on Kissat's remaining decisive wins.

## Claim boundary

This negative ablation is scoped to pinned Kissat 4.0.4, the exact twelve selected SAT Competition 2026 instances, the fixed 60-second solver budget, and the pre-registered materiality rule. It does not establish SatSolv superiority or make a universal claim about XOR preprocessing.
