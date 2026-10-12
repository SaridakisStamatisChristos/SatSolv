# Clause Database v2.1: bounded quality retention experiment

Control: Core v2 `b11f47060b99815620569e5fe2ad6b8071e5db01`.
Branch: `exp/satsolv-v2-clause-db-v21`. No change to Core v2 or main.
Rejected reference: Clause DB v2 `ae855f9da6c6222a6ca9ec495c0b7836d21cad50`.

## Hypothesis and policy

The rejected v2 policy may have lost throughput through cumulative retention
pressure. This is a hypothesis, not an established causal finding. This experiment
restores Core-v2 half-pool deletion pressure while retaining dynamic quality:

- LBD <=2 and binary/unit learnt clauses remain CORE; reasons remain locked.
- New non-CORE clauses start LOCAL, including initial LBD3/4 clauses.
- TIER2 promotion requires LBD <=4 plus improvement or two analysis uses in
  distinct conflicts. Repeated visits during the same conflict do not count twice.
- A TIER2 clause idle for one reduction interval R is demoted, including locked
  clauses (the lock still forbids deletion). Demotion resets its recent use count.
- At each reduction, unlocked TIER2 protection is capped at min(R, E/4), where E
  is the eligible unlocked nonbinary non-CORE pool. Excess clauses are demoted.
- Delete floor(E/2) LOCAL clauses. The cap guarantees enough LOCAL candidates.
  There is no universal creation grace or idle exemption as in rejected v2.
- Worse clauses rank by LBD, oldest touch, lowest activity, oldest creation, size,
  and finally stable clause ID for deterministic ties.

Locked clauses may temporarily exceed the unlocked TIER2 cap. CORE and binary
growth remain outside this cap: this is bounded discretionary retention, not a
claim of bounded total memory. The existing arena compaction remains unchanged.

LBD only improves. Quality is updated during conflict analysis with LBD counting
fused into the existing traversal. Cold metadata lives in a parallel stable-ID
array, preserving the Core-v2 hot clause descriptor layout. Propagation, proof
emission, minimization, phase saving, EVSIDS and Luby restarts are unchanged.

## Telemetry

JSON statistics include final CORE/TIER2/LOCAL counts, promotions/demotions,
deletions by tier, formerly-TIER2 clauses actually reused while TIER2 before their
eventual deletion, average deletion age in conflicts, and reductions. Tier counts
must sum to generated minus deleted learnt clauses. CORE deletion must be zero.

Peaks of learnt clauses, TIER2, arena literals and live logical watchers are
sampled before/after reductions and at solver exit; they are sampled peaks, not
continuous maxima. The logical watcher count excludes dead lazy watcher entries.
Database bytes estimate allocated clause descriptors, cold quality metadata and
literal arena capacities; it excludes other solver structures and is not RSS.
No tracing or additional database scan is added to the propagation path.

## Prespecified execution and gates

Workflow: `.github/workflows/clause-db-v21.yml`.

1. Strict CMake release and sanitizer builds, policy tests, existing exhaustive,
   metamorphic, parser, fail-closed harness, SAT witness, external DRAT and
   MiniSat/Kissat differential checks. Add external proof checking after actual
   database deletion using a generated exhaustive blocking formula.
2. Same frozen 10-instance diagnostic set and 15 s/4 GiB/180 s proof budget as
   the preceding clause-DB screen. Compare exact Core v2, candidate and rejected
   v2 on the same runner with the unchanged schema-4 harness.
3. Screen requires no certification failures, no solved-count regression, no
   lost baseline solve, PAR-2 <=1.03 times control, and lower aggregate endpoint
   live learnt population than v2. Report throughput, search work and proof bytes.
   Endpoint populations describe observed pressure; differing search trajectories
   mean they do not alone establish the causal mechanism.
4. If screen passes, use the unchanged acceptance limit of 60 s/4 GiB/300 s proof
   checking for clqcolor, vmpc and rbsat. Compare exact control versus candidate.
   No lost baseline solve, no solved-count regression, same 3% PAR-2 gate.
5. Only after both focused gates pass, run the original full frozen 30-instance
   acceptance comparison with pinned MiniSat/Kissat and unchanged limits. Require
   >=8/30, PAR-2 <90.93, zero certification failures/crashes/errors.

The 60-second guard is separate from the unchanged 15-second screen. It tests
the handoff's long-running protected solves without claiming equivalence between
different time limits. The full threshold uses the handoff's approximate historic
reference; a marginal improvement alone cannot establish statistical superiority.
No branch is automatically merged regardless of gate outcome.

## Verification improvements

New internal tests directly exercise stale eviction, reuse-gated promotion,
same-conflict accounting, bounded protection, exact half deletion, reason/binary/
CORE preservation, compaction, reset, monotonic LBD and telemetry conservation.
They use explicit exceptions, including in Release. Existing assert-based unit
tests are also compiled with NDEBUG undefined; previously Release could remove
those assertions. This test-only change does not alter solver optimization flags.

If rejected, retain artifacts and return to Core v2. Do not parameter-sweep this
policy or combine it with rejected restart control. The next planned axis is
nonrecursive conflict-clause minimization, after reconstructing the previously
rejected recursive implementation.
