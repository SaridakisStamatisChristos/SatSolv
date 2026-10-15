# Kissat structural mechanism attribution

Status: **completed diagnostic campaign; narrow candidates identified, external validation required before implementation**.

Base lineage: validated deterministic target phases, followed by the completed XOR-congruence NO-GO experiment.

## Motivation

The deterministic target-phase policy reproducibly improved the frozen 30-instance result from 9 to 10 certified solves with no lost control solve, while pinned Kissat 4.0.4 remained at 15. A subsequent external SAT Competition 2026 ablation of Kissat's `congruencexors` option produced 12/12 neutral cases, so XOR-gate extraction is not justified as SatSolv's next intervention.

The next question was narrower and causal: **which Kissat structural subsystem is necessary for more than one of its remaining decisive wins over SatSolv?**

This campaign performs necessity attribution before any new SatSolv feature is written.

## Frozen diagnostic cases

The campaign uses six instances from the existing 2023 frozen benchmark manifest on which pinned Kissat 4.0.4 previously solved within 60 seconds and the validated SatSolv target-phase candidate did not:

- `DivS_568_11.cnf.sanitized.cnf` — SAT;
- `brent_69_0.05.cnf` — SAT;
- `SAT_dat.k10.cnf` — UNSAT;
- `mchess16-mixed-35percent-blocked.cnf` — UNSAT;
- `posixpath__joinrealpath_13.cnf` — UNSAT;
- `or_randxor_k3_n520_m520.cnf` — UNSAT.

These are **diagnostic**, not a new holdout. Any mechanism that survives here must subsequently pass a separately pre-registered external validation set before implementation/adoption claims.

## Same-binary ablations

Pinned comparator: Kissat 4.0.4 at `8af8e56f174b778aef3aa45af9f739b2a5f492c2`.

Every case runs the same Kissat executable under the following arms:

| Arm | Disabled mechanism | Exact option |
|---|---|---|
| `control` | none | default |
| `no_eliminate` | bounded variable elimination | `--eliminate=0` |
| `no_probe` | probing | `--probe=0` |
| `no_vivify` | clause vivification | `--vivify=0` |
| `no_substitute` | equivalent-literal substitution | `--substitute=0` |
| `no_congruence` | gate congruence | `--congruence=0` |
| `no_sweep` | SAT sweeping | `--sweep=0` |
| `no_factor` | bounded variable addition | `--factor=0` |
| `no_simplify` | broad probing + elimination umbrella | `--simplify=0` |

The broad `no_simplify` arm is intentionally an **interaction detector**, not a narrow attribution. If it passes while its narrow components do not, the correct next action is to decompose the interaction rather than implement a generic simplifier.

## Execution and certification contract

- frozen 30-instance manifest SHA-256: `7ef151bdfce7cd0344e27643446f2509823b43432a64292661a6de86ceb26a53`;
- one exact Kissat binary per case for all arms;
- seed 1;
- 60-second solver cutoff;
- 4 GiB memory limit;
- 300-second proof-verification cutoff;
- SAT results require independent model validation;
- UNSAT results require independent DRAT verification with pinned `drat-trim`;
- a default-control failure to reproduce the known Kissat win invalidates that case rather than silently treating it as performance evidence;
- arm order is deterministically rotated by case index to reduce systematic position effects without adapting to observed outcomes.

A material degradation occurs when disabling a mechanism causes:

1. the default control to solve while the ablated arm does not; or
2. both arms to solve, but the ablated arm takes at least 2.0x the control runtime.

A material reverse is defined symmetrically.

## Pre-registered attribution threshold

A narrow mechanism becomes an **attribution candidate** only if:

- disabling it causes material degradation on at least **two** of the six decisive Kissat wins; and
- material degradations outnumber material reversals.

This threshold identifies a mechanism worth validating; it is **not** sufficient evidence to implement or adopt it in SatSolv.

Aggregate decisions:

- `VALIDATE_NARROW_CANDIDATES_ON_HOLDOUT` — one or more narrow mechanisms pass the threshold;
- `DECOMPOSE_SIMPLIFICATION_INTERACTION` — only the broad `no_simplify` arm passes;
- `NO_ATTRIBUTED_STRUCTURAL_MECHANISM` — no structural subsystem passes the pre-registered threshold.

## Observed result — 2026-10-15

GitHub Actions run `38677127813` completed all six cases and the aggregate decision with no integrity errors. Every definitive SAT result passed independent model checking and every definitive UNSAT result passed external DRAT verification.

Aggregate decision: **`VALIDATE_NARROW_CANDIDATES_ON_HOLDOUT`**.

| Mechanism | Material degradations | Reversals | Threshold |
|---|---|---:|---|
| probing | `brent`, `SAT_dat.k10`, `posixpath`, `or_randxor` | 0 | **PASS (4/6)** |
| gate congruence | `brent`, `posixpath` | 0 | **PASS (2/6)** |
| bounded variable addition / factoring | `posixpath`, `or_randxor` | 0 | **PASS (2/6)** |
| equivalent-literal substitution | `brent` | 0 | FAIL (1/6) |
| SAT sweeping | `DivS` | 0 | FAIL (1/6) |
| bounded variable elimination | none | 0 | **FAIL (0/6)** |
| vivification | none | 0 | **FAIL (0/6)** |
| broad `simplify=0` interaction arm | `brent` | 0 | FAIL (1/6) |

### Important causal findings

1. **Generic probing is the strongest common dependency**, with four independently certified material degradations and no reversals. This does not authorize SatSolv's previous root-probing implementation: Kissat's probing phase is an orchestration layer that invokes congruence, substitution, backbone reasoning, vivification, sweeping, transitive reduction and factoring. The earlier SatSolv root-probe branch remains rejected on its own evidence.
2. **Gate congruence independently matters on two wins.** SatSolv already implements signed equivalence/SCC and complete AND-gate congruence, so the next question is which missing Kissat congruence submechanism matters (for example ITE or binary gate handling), not whether to add an undifferentiated congruence rewrite.
3. **Factoring/BVA independently matters on two wins.** This is a materially different mechanism from BVE and has proof/model implications because it can introduce variables. It requires external validation and a dedicated proof-system design before any implementation attempt.
4. **Plain BVE is neutral on all six cases.** In particular, disabling Kissat elimination does not materially hurt `posixpath`, despite the earlier observational fact that Kissat eliminated many variables there. This falsifies the simple explanation that SatSolv's missing feature is ordinary BVE.
5. **Vivification is neutral on all six cases.** The earlier rejected SatSolv vivification branch should remain rejected absent new evidence.
6. **No broad simplification interaction signal passed.** The useful evidence comes from the narrow arms, not from `simplify=0`.

## Decision

Do not implement BVE, vivification, XOR congruence, Gaussian elimination, or generic root probing from these results.

Proceed to a **disjoint external validation campaign** for the three narrow candidates:

- probing orchestration;
- gate congruence;
- factoring / bounded variable addition.

Only mechanisms that reproduce a material effect on a pre-registered external holdout are eligible for a SatSolv implementation experiment. A passing holdout still does not establish superiority; implementation must then preserve proof/model correctness and beat the validated target-phase control under a separate same-binary candidate qualification.

## Hard inference boundaries

- Do not infer BVE or probing from a broad `no_simplify` result without the corresponding narrow arm.
- Do not resurrect SatSolv's previously rejected generic BVE/vivification experiments merely because a Kissat subsystem matters; implementation quality and exact mechanism still have to be established.
- Do not equate Kissat's `probe` option with SatSolv's previous root-probing experiment.
- Do not implement a mechanism that fails the external validation stage.
- Do not use this frozen diagnostic campaign as evidence of general SOTA or SatSolv superiority.
