# SatSolv Core v2

Core v2 is an audit-driven rewrite of SatSolv's performance-critical storage, propagation, proof-output, and conflict scratch paths. It intentionally preserves the v1 search policy so performance effects can be attributed to the engine rather than to heuristic changes.

## Objectives

1. reduce pointer chasing and per-clause heap allocation in BCP;
2. make binary clauses a dedicated fast path;
3. avoid rescanning long clauses from the beginning on every watch relocation;
4. make learnt-clause lock checks O(1);
5. reclaim deleted learnt-clause literal storage without invalidating clause IDs used by reasons and watches;
6. remove avoidable hot-path hash allocations from LBD/minimization;
7. retain independently checkable UNSAT proofs while reducing proof-writing overhead.

## Design

### Packed literal arena

Clause metadata is stored separately from literals. Every clause owns an offset and size into one contiguous `literal_arena_`. Clause IDs remain stable even when the arena is compacted, so reason references and watcher references do not need remapping.

Deleted learnt clauses contribute to `dead_literals_`. Once dead storage is both material and at least one quarter of the arena, live literals are compacted and every live clause offset is rewritten. Deleted clauses are never valid assignment reasons because `reason_refs` prevents deletion while locked.

### Propagation

Binary clauses use `BinaryWatcher { clause, implied }`, allowing direct implication/conflict checks without scanning clause memory.

Clauses of length three or greater use `Watcher { clause, blocker }`. A blocker hit avoids literal-arena access. Watched literals are kept in positions 0 and 1 of each live long clause. `scan_cursor` records where replacement search should resume, so scans do not always restart at literal 2.

### Lock accounting

Every clause tracks `reason_refs`. Enqueueing an implication increments the referenced clause; backtracking decrements it. Database reduction therefore checks a constant-time counter instead of scanning all variables for every deletion candidate.

### Conflict scratch state

LBD level membership and conservative minimization membership use reusable epoch-mark arrays. First-UIP analysis uses reusable byte marks plus a touched-variable list. This removes repeated `unordered_set` creation and large per-conflict zero-initialization while retaining v1 logical behavior.

### Proof output

UNSAT proof output uses binary DRAT encoding. The earlier controlled experiment showed lower proof size and higher conflict throughput while remaining accepted by pinned `drat-trim`. Core v2 counts emitted bytes directly and no longer calls `tellp()` for each learnt clause.

## Deliberate non-goals

Core v2 does not change:

- EVSIDS decay policy;
- phase saving;
- Luby restart policy;
- learnt-clause reduction ranking;
- LBD retention thresholds;
- preprocessing/inprocessing;
- rephasing;
- stable/focused search modes;
- MAB/portfolio selection.

Those are separate search-engine campaigns after the core is qualified.

## Acceptance

Core v2 is not mergeable merely because it compiles. Required evidence is:

- unit, exhaustive/random, metamorphic and parser-adversarial checks green;
- ASan/UBSan green;
- MiniSat/Kissat differential checks green;
- all counted UNSAT results accepted by pinned `drat-trim`;
- zero solver errors, crashes, or verification failures in the frozen acceptance harness;
- direct A/B evidence against `r7/zero-to-hero` inspected;
- full 30-instance SAT Competition 2023 slice inspected for solved count and PAR-2.

A throughput improvement alone is not sufficient if solved-instance coverage regresses.
