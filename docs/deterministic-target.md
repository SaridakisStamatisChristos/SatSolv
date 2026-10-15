# Deterministic target-phase causal qualification

Status: **experimental, off by default**. This campaign does not establish general superiority over Kissat and does not authorize a merge into `main`.

## Why this campaign exists

The preceding structural-search campaign left one search-policy signal worth isolating: conflict-only delayed target phases twice improved the frozen-30 paired result from 9 certified solves to 10, while the pinned Kissat comparator remained at 15. Elapsed-time activation was less stable across the confirmation repetitions. Those observations came from the existing frozen development corpus, so they are evidence for a discriminating experiment, not an independent validation population.

The next question is deliberately narrower:

> Does conflict-count activation of target phases improve SatSolv when control and candidate are the exact same executable and differ only by runtime flags?

Previous comparisons sometimes crossed source commits. Even when the semantic change is known, a cross-build comparison permits code layout, compiler placement, or unrelated source drift to become a confounder. This campaign removes that confound.

## Preserved target-phase semantics

An intermediate review questioned whether a target-window reset should clear every stored per-variable target phase. That hypothesis was rejected after inspecting the exact pinned Kissat 4.0.4 source. Kissat's target-phase saver updates phases for currently assigned variables while retaining prior values for unassigned variables; rephasing separately resets the target-assigned height. Persistent target values are therefore an intentional part of the reference mechanism rather than evidence of a SatSolv defect.

SatSolv's target-phase semantics are **not changed** by this campaign.

## Exact experiment

One release build of SatSolv supplies both native arms:

- **control:** no experimental search flags;
- **candidate:** `--experimental-target-phases --search-warmup 1000000`;
- **comparator:** pinned Kissat 4.0.4 commit `8af8e56f174b778aef3aa45af9f739b2a5f492c2`.

`--search-warmup-seconds` is forbidden. The candidate can activate only when its conflict counter reaches one million. The control and candidate must have identical executable SHA-256 digests and identical source commits.

The initial qualification intentionally keeps the unchanged frozen 30-instance manifest, 60 solver seconds per instance, 4 GiB address-space limit, 300 proof-verification seconds, and seed 1. This is a **causal confirmation on the development corpus**, not the disjoint holdout required for a broader claim.

`tools/deterministic_target_ab.py` creates the paired-campaign configuration and independently audits the resulting evidence for:

1. same control/candidate source commit;
2. same control/candidate executable path and digest;
3. exact conflict-only candidate flags;
4. absence of wall-clock activation;
5. zero target-policy activity in the control;
6. no candidate activation before the conflict threshold;
7. no target-policy work before candidate activation;
8. successful paired-campaign correctness certification.

A negative performance result is retained as valid evidence. Causal-integrity or correctness failures are not.

## Correctness contract

The campaign preserves the existing solver contract:

- SAT results count only after independent validation against the original CNF;
- UNSAT results count only after external proof verification;
- solver errors, invalid models, rejected proofs, malformed statistics, and hash/pin violations fail closed;
- target phases change decision polarity only and do not relax proof requirements;
- all experimental switches remain off by default.

Before the performance run, CI executes the native test suite and independent search-policy truth/model/RUP/DRAT checks against the same candidate build.

## Hypothesis and falsifier

**Hypothesis:** conflict-count-delayed target phases improve search trajectory on at least part of the current structured SAT slice without losing a control solve.

The preferred strategy is falsified on this development corpus if the same-binary candidate loses any certified control solve, solves fewer instances, or produces worse PAR-2. Even a positive frozen-30 result remains provisional until it survives a pre-registered, disjoint, family-balanced holdout.

A result that merely runs more conflicts per second is not considered support. The mechanism is about reducing or redirecting search work, not hot-loop throughput.

## Claim boundary

A passing paired gate means only that the conflict-only target policy numerically non-regressed on this exact frozen corpus under this exact budget. Strict numerical superiority over the pinned Kissat arm additionally requires more certified solves **and** lower PAR-2, but even that would not establish statistical or general SOTA superiority.

No `README` superiority claim should be added from this campaign. A broader claim requires a separately frozen external holdout, family-aware analysis, longer-budget confirmation, current comparator configurations, and clean independent reproduction.
