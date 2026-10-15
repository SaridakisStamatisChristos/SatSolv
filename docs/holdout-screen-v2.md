# External holdout v2 — control-only qualification screen

Status: **pre-registered control-only screen; no structural ablation is authorized in this branch**.

## Why v2 exists

External structural holdout v1 completed all 18 cases but failed its own evidence contract:

- only 8/18 default Kissat controls independently certified within the fixed limits, below the pre-registered minimum of 10;
- `stable-semantics-03` returned default UNSAT but its control proof did not certify within the 300-second verifier budget;
- aggregate decision: `INVALID_EVIDENCE`;
- no candidate mechanism was externally validated.

The correct repair is the holdout construction, not SatSolv solver code and not post-hoc threshold relaxation.

## Separation principle

V2 has two strictly separated stages.

### Stage A — this branch

Observe **only pinned default Kissat 4.0.4** on a predeclared external candidate pool. No `--probe=0`, `--congruence=0`, `--factor=0`, or any other mechanism-ablation option exists in this stage.

The sole purpose is to identify external cases on which the control is both:

1. independently certifiable; and
2. comfortably inside the later 60-second ablation cutoff.

### Stage B — future branch only after Stage A passes

Freeze the exact Stage-A selected list, GBD identities, decompressed CNF SHA-256 values, truth labels, and selection order in a new commit. Only then may the structural ablations be executed.

This prevents performance outcomes from influencing case selection.

## Candidate pool

Source metadata is pinned to:

`satcompetition/2026@3e2dee7cf5aa643c77bf946280a3c1c520756ef9`.

The fixed pool contains **36 SAT Competition 2026 instances**: six candidates from each of six families not used in the prior XOR holdout or structural holdout v1.

SAT families:

- `allowable-seqence` (spelling preserved from the competition metadata);
- `boxfolding`;
- `cyclic-anti-bandwidth`.

UNSAT families:

- `multiplier-circuit-miters`;
- `exam-scheduling`;
- `datapath-equivalence-checking`.

Every candidate has a pre-registered SAT/UNSAT label, GBD hash, and isohash2 identity in `benchmarks/holdout_screen_v2.json`.

## Common control contract

Pinned comparator:

- Kissat 4.0.4 commit `8af8e56f174b778aef3aa45af9f739b2a5f492c2`;
- seed 1;
- 30-second screen solver budget;
- 4 GiB memory;
- 300-second proof-verification budget;
- SAT requires independent model validation;
- UNSAT requires pinned external `drat-trim` verification.

A timeout is a valid negative screen observation. A definitive result that disagrees with the fixed truth label or fails independent certification is an integrity failure.

## Pre-registered selection rule

For each family, preserve the exact candidate order committed in the JSON specification.

A candidate is **eligible** only when default pinned Kissat:

- returns the fixed expected truth;
- finishes within the 30-second screen limit;
- independently certifies under the common model/DRAT contract.

Select the **first three eligible candidates per family**. No ranking by exact runtime and no ablation result is allowed.

Stage A passes only if:

- all 36 candidate observations are present and integrity-clean;
- every one of the six families supplies at least three eligible cases;
- exactly 18 cases are selected;
- the resulting truth balance is exactly 9 SAT / 9 UNSAT.

Possible decisions:

- `FREEZE_SELECTED_HOLDOUT` — commit the exact selected set in a separate branch before any ablation;
- `SCREEN_INSUFFICIENT` — do not substitute another family, increase cutoffs, or relax certification after seeing results. A new screen must be separately pre-registered.

## Why 30 seconds

The later mechanism-ablation campaign retains a 60-second solver cutoff. Requiring the default control to solve within 30 seconds provides a substantial runtime margin and reduces the chance that normal host variability turns selected controls into boundary timeouts.

## Claim boundary

A successful control screen says nothing about whether probing, congruence, or factoring is beneficial. It only constructs an externally certifiable test set without observing those interventions. No SatSolv core change, mechanism adoption, or superiority claim is authorized by this screen.
