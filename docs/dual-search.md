# Dual-search v1: focused VMTF / stable EVSIDS

Status: **experimental, off by default**. This branch tests one search-orchestration hypothesis and does not establish superiority over Kissat.

## Hypothesis

The current SatSolv gap is dominated by search trajectory rather than by another isolated preprocessing primitive. The existing delayed target-phase campaign supplies the qualified control. Dual-search v1 changes only the decision-variable ordering regime:

- **focused mode:** intrusive variable-move-to-front (VMTF) queue;
- **stable mode:** the existing indexed EVSIDS heap;
- **switching:** deterministic conflict-count epochs, default 1,000 conflicts.

The experiment does **not** change Luby restart policy, target-phase semantics, learnt-clause reduction, preprocessing, simplification, proof generation, SAT validation, or UNSAT certification.

## Runtime interface

Dual search is enabled only with:

```text
--experimental-dual-search
```

The mode epoch can be set with:

```text
--dual-search-interval N
```

`N` must be positive. With the experimental flag absent, the VMTF queue does not participate in decisions and the existing EVSIDS path remains the solver policy.

## Focused VMTF semantics

The queue is intrusive over variables. Its tail is the highest-priority variable. Conflict-analysis variable bumps move the corresponding variable to the tail and advance a monotonic stamp. Assigned variables stay linked and are skipped by the search cursor. Backtracking restores the cursor toward newly unassigned variables by stamp.

The initial queue is ordered from the same activity/rank state as the EVSIDS heap, so before conflict-driven bumps the focused mode begins from an ordering compatible with the existing score state rather than an unrelated variable-number order.

Both the EVSIDS heap and VMTF queue stay updated while dual search is enabled. Switching therefore changes the decision policy without rebuilding or discarding the other policy's state.

## Causal qualification

The frozen-30 campaign uses one compiled SatSolv executable for both native arms.

Control:

```text
--experimental-target-phases --search-warmup 1000000
```

Candidate:

```text
--experimental-target-phases --search-warmup 1000000 \
--experimental-dual-search --dual-search-interval 1000
```

The control and candidate must have identical executable SHA-256 digests and identical source commits. Pinned Kissat 4.0.4 remains an external comparator. The common resource contract is 60 solver seconds, 4 GiB solver memory, seed 1, and 300 seconds for independent proof checking.

The causal auditor additionally requires:

- zero dual-search activity in the control;
- candidate focused+stable conflict counts equal total conflicts;
- candidate focused+stable decision counts equal total decisions;
- focused candidate decisions exercise VMTF traversal;
- mode switches cannot exceed the deterministic conflict schedule;
- both native arms preserve the same delayed target-phase activation contract;
- the paired campaign passes the existing independent SAT-model / UNSAT-proof certification contract.

## Adoption and falsification rule

Dual-search v1 is eligible for the next stage only if all of the following hold on the frozen development corpus:

1. no certified control solve is lost;
2. at least one previously unsolved control instance is gained;
3. the candidate solves at least 11/30;
4. the candidate solves strictly more instances than the delayed-target control;
5. candidate PAR-2 is no worse than control.

A 10/30 result with useful frontier movement may motivate a redesigned experiment, but it does not authorize adoption. A regression in solved count, loss of a control solve, or absence of useful frontier movement falsifies this version of the strategy.

Even a candidate result above pinned Kissat on this frozen corpus would establish only a benchmark-slice result. Statistical or general SOTA claims require the separately frozen external holdout, longer-budget confirmation, current comparators, and independent reproduction.

## Next stage if v1 passes

Only after this gate passes should the next PR alter restart policy. The intended sequence remains:

1. dual variable-selection modes;
2. mode-specific restart policies and trail reuse;
3. richer rephasing;
4. learnt-clause ecology;
5. adaptive mode/rephase control.

This ordering keeps attribution possible and makes rollback trivial: leave `--experimental-dual-search` off.
