# Core v2 certified A/B evidence

## Scope

This record qualifies the first audit-driven Core v2 engine rewrite against the verified R7 baseline. It does **not** claim whole-corpus or broader SAT-solver superiority.

- Baseline commit: `54897a9ca169e760d82124c245675f04a28fd005` (`r7/zero-to-hero`).
- Candidate workflow commit: `cdb6bd40d66b274eda233e12a8a2b3a2630870de` (`fit/satsolv-v2-core`).
- Workflow run: `38264052703` (`Core v2 A/B`).
- Artifact: `core-v2-ab-evidence`, artifact id `11754007253`.
- Artifact digest: `sha256:49f18b31463c57ab35ff295330337c8241dea433e07338f96737b5c507c7c42b`.
- Corpus: 10 frozen hard instances selected from the committed SAT Competition 2023 acceptance manifest.
- Solver budget: 12 s wall clock per instance.
- Memory limit: 4 GiB.
- UNSAT proof checking: external pinned `drat-trim` with a separate verification budget.

The current Core v2 head contains no subsequent C++/CMake solver change after the candidate used by this A/B; later commits only hardened evidence tooling/workflows/documentation.

## Certification

Both baseline and candidate artifacts report:

- `certification.passed=true`;
- zero certification violations;
- zero crashes;
- zero solver errors;
- zero verification failures.

Solve coverage on the 12 s diagnostic slice is unchanged at **1/10** for both variants.

## Aggregate result

| Metric | R7 baseline | Core v2 | Change |
|---|---:|---:|---:|
| Aggregate conflicts/s | 18,976.12 | 32,873.70 | **1.732x** |
| Aggregate propagations/s | 1,889,817.89 | 3,344,075.29 | **1.770x** |
| Proof bytes | 368,859,970 | 307,248,935 | **-16.7%** |
| Diagnostic PAR-2 | 22.77095 | 22.64157 | improved |
| Solved | 1/10 | 1/10 | unchanged |

The one instance solved by both variants, `clqcolor-08-06-07.shuffled-as.sat05-1257.cnf`, was externally certified UNSAT:

- baseline elapsed: **11.7095 s**;
- Core v2 elapsed: **10.4157 s**;
- Core v2 is approximately **11.0% faster** on that solved case.

## Per-instance search-work evidence

At the same 12 s budget Core v2 processes substantially more conflicts on all ten rows, including approximately:

- `mchess16-mixed-35percent-blocked.cnf`: 242,000 -> 655,099 conflicts;
- `posixpath__joinrealpath_13.cnf`: 260,173 -> 474,259 conflicts;
- `or_randxor_k3_n520_m520.cnf`: 619,522 -> 939,507 conflicts;
- `brent_69_0.05.cnf`: 44,000 -> 134,000 conflicts;
- `clqcolor-08-06-07.shuffled-as.sat05-1257.cnf`: 515,703 -> 722,440 conflicts while solving faster.

This is strong evidence that the packed clause arena, binary propagation path, blocker watchers, O(1) reason locking, reusable conflict scratch state and binary proof writer materially improve engine throughput.

## Decision

**Core-v2 engine direction: PASS for continued qualification.**

This A/B is not sufficient to merge by itself. The merge gate remains the frozen 30-instance acceptance run because higher throughput is not acceptable if it loses full-corpus solves or worsens the primary comparative metrics.
