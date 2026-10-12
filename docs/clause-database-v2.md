# Clause Database v2

Clause Database v2 is a controlled experiment stacked on the accepted Core v2 propagation engine.

## Decision

Replace the single learned-clause deletion pool with three quality tiers while leaving branching, phase saving, conflict analysis, and restart scheduling unchanged.

- **CORE**: learned clauses with LBD <= 2. Never selected for database reduction.
- **TIER2**: learned clauses with LBD 3-4. Retained aggressively and considered for deletion only after a long grace period and sustained inactivity.
- **LOCAL**: learned clauses with LBD > 4. Eligible for earlier reduction after a shorter grace period and inactivity window.

Binary learned clauses remain protected independently of tier because the dedicated binary propagation path makes them cheap and strategically valuable.

## Dynamic quality

When a learned clause participates in conflict analysis, SatSolv records that it was touched and recomputes its LBD while it is still above the CORE threshold. LBD is monotonic: it may improve but never worsen. An improved LBD can promote a clause from LOCAL to TIER2 or CORE.

The current conflict count records both clause creation and last conflict-analysis use. Reduction therefore considers clause quality and temporal usefulness instead of ranking every non-binary learned clause in one global pool.

## Reduction policy

With `reduce_interval = R`:

- LOCAL clauses receive a `2R` creation grace period and must be idle for at least `R` conflicts; the worst half of eligible clauses are deleted.
- TIER2 clauses receive a `4R` creation grace period and must be idle for at least `2R` conflicts; the worst quarter of eligible clauses are deleted.
- CORE clauses are retained.

Within an eligible tier, worse clauses are ordered by higher LBD, older last-touch time, lower activity, then larger size.

Locked reason clauses are never deleted. Stable clause IDs, watcher semantics, exact-capacity behavior, binary DRAT generation, and arena compaction remain unchanged.

## Why this is isolated

The preceding Core v2 acceptance run improved SatSolv from 7/30 to 8/30 with zero certification failures. Clause Database v2 deliberately does **not** modify EVSIDS, saved phase, or Luby restarts in the same experiment. This makes any change in search trajectory attributable to learned-clause retention rather than a mixed search-policy patch.

## Acceptance gates

Before this branch is eligible to stack further search-control work:

1. strict build and unit tests pass;
2. exhaustive/random, metamorphic, differential, sanitizer, SAT-witness, and external `drat-trim` certification remain green;
3. focused Core-v2-vs-Clause-DB-v2 A/B shows no solved-count regression and no material aggregate PAR-2 regression;
4. the full frozen 30-instance acceptance corpus is rerun before merge;
5. no SOTA claim is made from the focused diagnostic corpus.
