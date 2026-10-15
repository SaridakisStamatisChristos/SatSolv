# External holdout v2 — control-only qualification screen

Status: **completed; `SCREEN_INSUFFICIENT`. No structural ablation or SatSolv implementation is authorized.**

## Why v2 exists

External structural holdout v1 completed all 18 cases but failed its own evidence contract:

- only 8/18 default Kissat controls independently certified within the fixed limits, below the pre-registered minimum of 10;
- `stable-semantics-03` returned default UNSAT but its control proof did not certify within the 300-second verifier budget;
- aggregate decision: `INVALID_EVIDENCE`;
- no candidate mechanism was externally validated.

The correct repair was the holdout construction, not SatSolv solver code and not post-hoc threshold relaxation.

## Separation principle

V2 separated case qualification from intervention testing.

### Stage A — this branch

Observe **only pinned default Kissat 4.0.4** on a predeclared external candidate pool. No `--probe=0`, `--congruence=0`, `--factor=0`, or any other mechanism-ablation option exists in this stage.

The sole purpose was to identify external cases on which the control was both:

1. independently certifiable; and
2. comfortably inside the later 60-second ablation cutoff.

### Stage B — only after Stage A passes

Freeze the exact Stage-A selected list, GBD identities, decompressed CNF SHA-256 values, truth labels, and selection order in a new commit. Only then may structural ablations execute.

Stage A did **not** pass, so Stage B is not authorized from v2.

## Candidate pool

Source metadata was pinned to:

`satcompetition/2026@3e2dee7cf5aa643c77bf946280a3c1c520756ef9`.

The fixed pool contained **36 SAT Competition 2026 instances**: six candidates from each of six families not used in the prior XOR holdout or structural holdout v1.

SAT families:

- `allowable-seqence` (spelling preserved from the competition metadata);
- `boxfolding`;
- `cyclic-anti-bandwidth`.

UNSAT families:

- `multiplier-circuit-miters`;
- `exam-scheduling`;
- `datapath-equivalence-checking`.

Every candidate had a pre-registered SAT/UNSAT label, GBD hash, and isohash2 identity in `benchmarks/holdout_screen_v2.json`.

## Common control contract

Pinned comparator:

- Kissat 4.0.4 commit `8af8e56f174b778aef3aa45af9f739b2a5f492c2`;
- seed 1;
- 30-second screen solver budget;
- 4 GiB memory;
- 300-second proof-verification budget;
- SAT required independent model validation;
- UNSAT required pinned external `drat-trim` verification.

The workflow built Kissat and `drat-trim` once and distributed the same SHA-256-checked executables to every matrix job. A timeout was a valid negative screen observation. A definitive result that disagreed with the fixed truth label or failed independent certification was an integrity failure.

## Pre-registered selection rule

For each family, preserve the exact candidate order committed in the JSON specification.

A candidate was **eligible** only when default pinned Kissat:

- returned the fixed expected truth;
- finished within the 30-second screen limit;
- independently certified under the common model/DRAT contract.

Select the **first three eligible candidates per family**. No ranking by exact runtime and no ablation result was allowed.

Stage A could pass only if:

- all 36 candidate observations were present and integrity-clean;
- every one of the six families supplied at least three eligible cases;
- exactly 18 cases were selected;
- the resulting truth balance was exactly 9 SAT / 9 UNSAT.

Possible decisions:

- `FREEZE_SELECTED_HOLDOUT` — commit the exact selected set in a separate branch before any ablation;
- `SCREEN_INSUFFICIENT` — do not substitute another family, increase cutoffs, or relax certification after seeing results. A new screen must be separately pre-registered.

## Observed result — 2026-10-15

GitHub Actions run `38679143364` completed the common comparator build, all **36 / 36** control-only candidate observations, and the aggregate selection step.

Aggregate result:

- integrity: **PASS**;
- integrity errors: **0**;
- candidate observations: **36 / 36**;
- eligible candidates: **0 / 36**;
- selected cases: **0**;
- selected truth balance: **0 SAT / 0 UNSAT**;
- every family: **0 / 6 eligible**;
- selection decision: **`SCREEN_INSUFFICIENT`**.

Representative observations were clean default-control timeouts at the fixed 30-second boundary, including candidates from both SAT and UNSAT families. Examples include `allowable-seqence-03`, `boxfolding-03`, `cyclic-anti-bandwidth-04`, `multiplier-circuit-miters-05`, `exam-scheduling-03`, and `datapath-equivalence-checking` candidates. These were negative qualification observations, not certification failures.

The zero-eligibility outcome was adversarially sanity-checked against individual job logs. The jobs used the expected pinned default Kissat executable, no structural ablation options were present, executable SHA checks passed, and the representative cases genuinely exhausted the 30-second solver budget. Thus the aggregate result is not explained by the earlier v1 proof-certification defect or by an obvious screen-harness error.

## Interpretation

V2 changed the evidence path correctly but selected another pool dominated by hard competition instances. The 30-second margin rule was intentionally strict so the eventual 60-second same-binary ablation would not be dominated by control boundary failures. The evidence now says this particular candidate source/pool cannot satisfy that design.

Do **not** respond by:

- raising the 30-second limit on this already-observed pool;
- selecting the least-slow timeouts;
- changing the three-per-family rule;
- substituting families after observing this result;
- running `--probe=0`, `--congruence=0`, or `--factor=0` on the v2 candidates;
- treating workflow success as scientific selection success.

## Decision

**No holdout is frozen from v2. No structural mechanism implementation is authorized.**

A further repair must change the information source used to construct the candidate pool. The appropriate next evidence path is to use independent, pre-existing public solver-result data to identify external cases with strong control-runtime margin, then separately re-run pinned default Kissat as a control-only certification screen before freezing any holdout. Candidate discovery must remain blind to the future mechanism-ablation outcomes.

After two failed external-holdout constructions, another blind family sample from selected SAT Competition benchmarks would repeat the same premise and is therefore not justified.

## Claim boundary

This result does **not** refute the frozen-2023 diagnostic attribution of probing, congruence, or factoring. It says only that v2 failed to construct a sufficiently robust external validation set. The validated SatSolv target-phase result remains intact; XOR congruence, BVE, vivification and the old SatSolv root-probe experiment remain rejected on their own evidence.
