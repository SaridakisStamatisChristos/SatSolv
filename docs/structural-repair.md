# Comparative audit and structural repair

## Observed loss mechanisms

The full Core-v3 artifact (run 38460663113, commit
385c97cf1d55a5cd92ffd6241596d3857508424e) certifies 8/30, versus MiniSat 10/30
and Kissat 15/30. All original resource limits and input hashes are preserved.

| Frozen case | Full Core v3 | Pinned Kissat | Supported interpretation |
|---|---|---|---|
| rand_net | UNKNOWN at 60 s; 614,000 conflicts | UNSAT ~0.014 s; zero conflicts; 2,739 congruent variables | A structural reduction gap, directly supported by baseline profiling |
| clqcolor | UNSAT ~14.14 s; 1,032,480 conflicts | UNSAT ~0.512 s; 33,255 conflicts | Roughly 31x more conflicts; propagation throughput alone cannot explain/close the search gap |
| or_randxor | UNKNOWN at 60 s; 5,004,308 conflicts | UNSAT ~1.84 s; 86,046 conflicts, extensive vivification/inprocessing | Further search and simplification gaps remain; no claim that this patch solves them |
| posixpath | UNKNOWN at 60 s; 2,705,175 conflicts | UNSAT ~8.70 s; 379,391 conflicts, 73% variables eliminated | Further elimination/vivification/search work remains |

The completed 15-second ablation artifact identifies engine-only as the stronger
configuration (3/10, PAR-2 23.496) than the full combination (2/10, PAR-2 26.510).
The original candidate's probing and vivification produced zero successful
reductions on that diagnostic corpus. Binary minimization visited 356 million
edges to remove 2.31 million literals; its whole-system benefit was not established.
These are observed limitations, not evidence that those techniques never help.

## Changes and non-goals

The new candidate starts from the existing Core-v3 source and uses the observed
engine-only configuration by default: old simplification and binary minimization
off, clause decay 1.0. Those mechanisms remain explicitly experimental and tested;
they are not removed or declared globally invalid. Rejected historic restart and
recursive-minimizer experiments remain untouched.

The new independent congruence module implements:

1. iterative binary-implication SCC decomposition;
2. signed equivalence substitution with paired complement unions;
3. complete AND-gate recognition, including complemented inputs/outputs;
4. congruent-gate merging and degenerate/complementary-input reasoning;
5. proof additions before every semantic rewrite;
6. reverse restoration of original-variable SAT witnesses after any later BVE.

For an AND output o with inputs x_i, both (-o OR x_i) binary constraints and
(o OR -x_1 OR ... OR -x_k) must exist. Partial encodings never justify a merge.
Equivalent inputs are supported by already logged binary equivalence lemmas.
Gate-equivalence implications are RUP under opposite output assumptions.

A signed-cycle contradiction first emits a unit justified by failed-literal
propagation, then the empty clause. An empty clause alone is not necessarily RUP
when a binary SCC is inconsistent without a seed. Adversarial testing found this
proof-sequencing edge case; it was corrected before the candidate was pushed.

No Gaussian elimination, new auxiliary variables/RAT proof expansion, copied
Kissat code, adaptive restart controller or benchmark-specific branch is included.
Every instance receives the same deterministic global mechanism and budgets.

## Bounds and invariants

- Four million charged edge/candidate visits, at most 32 rounds, up to eight
  AND inputs. Initial normalization, binary indexing and whole-formula rewrites
  are additional bounded linear preparation per round, included in elapsed time.
- More than two million literals, one million clauses or 200,000 variables triggers a safe fallback
  to the unsimplified engine. No reduction is inferred from an incomplete SCC.
- Signed equivalence unions preserve complement mapping. IDs are never confused
  with a fixed truth value. Completed, proved unions remain valid when a later
  budget is exhausted. Original input objects are immutable.
- Programmatic formula inputs reject zero/out-of-range literals and invalid
  variable counts before signed indexing. Solver timing includes validation and
  initialization as well as preprocessing/search/proof flushing.
- Every replaced clause is emitted as a RUP addition before the working formula
  forgets its old form. Parent clauses remain available to the proof checker.
- Representative models are expanded to every original variable and checked
  independently against the original CNF. BVE reconstruction precedes equivalence
  restoration when experimental preprocessing is enabled.
- `--no-congruence` ablates the new module. Earlier mechanisms can be enabled with
  `--experimental-simplify`, `--experimental-binary-minimize`, and
  `--experimental-clause-decay`; explicit old `--no-*` switches remain available.

## Executed local evidence

Frozen rand_net input was downloaded from the original benchmark source and both
compressed and decoded SHA256 were verified. The candidate returned certified
UNSAT in ~0.17–0.25 seconds, with 2,733 equivalences and 86 conflicts, versus a
15-second unsolved engine control. The pinned drat-trim accepted its proof.

The local five-case diagnostic preserved the same 113,926-conflict vmpc solve
(~5.01 seconds) and 813,819-conflict clqcolor proof (~8.67 seconds). randxor and
posixpath remained UNKNOWN at 15 seconds. These are single-host diagnostics, not
a full-corpus acceptance result or a statistically established global speedup.

Executed correctness checks: strict GCC 13.3 build, 1,256 exhaustive/random
independent truth/model/RUP checks, 1,632 feature/seed checks, 636 structured
complete/incomplete/complemented gate and cycle proof/model checks with external
DRAT, native budget/interruption/model-extension tests, 1,200 metamorphic
transformations, parser/fail-closed harness regressions, and ASan/UBSan on 216
structural checks. LeakSanitizer is unavailable in this local container; remote
CI keeps leak detection enabled.

## Qualification and audit verdict

The structural workflow compares all 30 frozen instances with exact engine-only
control source 385c97cf1d55a5cd92ffd6241596d3857508424e. Arms alternate by instance
index, each at 60 seconds, 4 GiB and 300 seconds for independent proofs. It retains
every completed per-arm row, checks hashes/certification, and requires both the
original >=8 / PAR-2 <90.93 target and paired non-regression before adoption.
The standard full-corpus workflow also compares the candidate with the unchanged
pinned MiniSat/Kissat commands. A supplementary lane enables Kissat proofs.

Core correctness: strong executed evidence, with remote qualification pending for
this new source. Comparative SOTA: not established; the full previous candidate
trails both comparators. Implementation is not awarded SOTA Code Quality merely
for passing CI: material search-efficiency and proof-volume gaps remain relevant
to this performance-focused tool. Production/commercial/enterprise controls are
outside this campaign's core-algorithm scope.

Main and previous candidate/rejected branches remain unchanged. No merge is
authorized by a local diagnostic win. Required next evidence is the paired full
corpus and remote certification results, followed by independent raw inspection.
