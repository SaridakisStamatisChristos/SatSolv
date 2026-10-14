# Search reduction experiments

## Starting evidence and objective

The pinned structural candidate is f997df1bec87f045ffa48d16ab50729cd90e9a0e.
Its completed frozen-30 comparator run certifies 9 solves, PAR-2 85.119278,
versus MiniSat 8/30, 90.442813, and Kissat 14/30, 68.491158. Its paired
engine-control qualification passed. Comparative superiority over Kissat is
not established. The benchmark list, hashes, truth labels, 60-second search,
4-GiB process limit, and independent proof checking remain fixed.

This campaign tests causal search/reduction changes without replacing the
qualified structural defaults or resurrecting rejected restart/minimizer
experiments. No input-name-dependent policy, comparator substitution, hidden
portfolio, or changed resource limit is used.

## Inspected limitations

1. The old vivifier repeatedly scans the entire CNF, examines only the first
   128 clauses, and does not revisit the evolving learned database. Earlier
   ablations reported no successful probing/vivification reductions.
2. The old BVE rejects a variable based on the raw positive/negative pair
   count before rejecting tautologies or deduplicating resolvents. Its
   one-pass variable-ID schedule never returns to changed neighbors.
3. Learned clause LBD is recorded once. A clause that becomes stronger when
   reused remains ranked by its old quality during database reduction.
4. Search work and proof volume remain material: the last qualified full run
   generated about 8.30 GB of proof records, including unfinished searches.
   Fixing implementation throughput alone has not closed the search gap.

## Independently ablated changes

- `--experimental-watched-vivify`: reuse watched propagation with temporary
  assumptions at root level. Ignore the target clause during propagation;
  only completed propagation supports shortening. Add a RUP clause before
  deleting its parent. Replace watches, repropagate persistent root units,
  and restore saved phases after each pass. Locked reasons are never removed.
  An interrupted candidate is discarded. The search-time propagation template
  contains no work-budget/ignored-target checks when this feature is disabled.
- `--experimental-eliminate`: indexed min-product scheduling with one heap
  entry per variable; reschedule changed neighbors. Resolve complete occurrence
  sets, reject tautologies, deduplicate the actual resolvents, and require
  non-growing **clause count**. Literal growth is bounded globally by
  min(2,000,000, 4*original_literals+1,024), rather than incorrectly requiring
  every elimination to reduce literals. Log every resolvent before forgetting
  parents; retain parent clauses in the proof database. Reverse model extension
  precedes congruence reconstruction and follows any later old simplification.
- `--experimental-dynamic-lbd`: recompute distinct non-root decision levels
  when a learned clause participates in conflict analysis, and lower its stored
  LBD when justified. This only changes retention quality; no clause is
  strengthened or implied by an LBD update.

The watched vivifier is motivated by unit-propagation vivification in Li et al.,
*Clause Vivification by Unit Propagation in CDCL SAT Solvers*,
https://arxiv.org/abs/1807.11061. That prior work supports testing the mechanism;
its published improvements are not evidence that this implementation wins.

## Resource and failure boundaries

The initial vivification budget is 16 million charged visits. Later passes use
one-sixteenth of that budget at exponentially spaced conflict thresholds.
Candidate enumeration/sorting and root repropagation are additional bounded
work included in solver wall time. More than 200,000 variables, one million
descriptors, or four million arena literals bypasses vivification.

Elimination uses eight million charged resolution/occurrence visits; at most
64 occurrences per polarity and 128 parents per candidate; normalized
resolvents have at most 64 literals. More than 200,000 variables, one million
initial clauses, or two million input literals bypasses the pass. Historical
descriptors cannot exceed one million. New literal storage is bounded by
charged construction work, while the live literal-growth limit is enforced
before each commit. Empty resolvents are logged and stop preprocessing.
Incomplete occurrence collection or resolution never commits an elimination.

Both work limits are configurable for interruption tests. Budget zero disables
the corresponding pass. All three new features remain **off by default**.

## Executed exploratory results

Local 15-second screens are exploratory, single-run observations on a variable
load host; some correctness/build work overlapped. They are not adoption or
statistical-superiority evidence. Certified raw rows and flags are retained
in evidence/search-screens.json.

Watched vivification removes literals and passes independent checking but
lost the vmpc solve in its first screen (UNKNOWN at 15 seconds). It is rejected
as a default. More reductions alone do not establish a better solver.

The literal-non-growing elimination draft removed 216 variables on randxor but
none on posixpath. Inspecting the latter showed 4,264 binary clauses and 27
13-literal clauses: eliminating a variable can reduce clause count while
increasing literal count. The bounded-growth revision removed 27 variables
on posixpath and 222 on randxor; both still returned UNKNOWN at 15 seconds.
It retained vmpc's 113,926-conflict SAT trajectory. Rand_net became externally
certified UNSAT with zero search conflicts after elimination.

Dynamic LBD certified clqcolor with 469,242 conflicts versus the pinned
control trajectory's 813,819 (about 42% fewer conflicts), but its vmpc screen
returned UNKNOWN at 15 seconds. This is a real search tradeoff, not an
established aggregate win. Neither posixpath nor randxor was solved yet.

Independent executed checks include 1,640 budget/seed/original-truth/model/RUP
checks for each new structural pass (external drat-trim on UNSAT), 840 combined
checks, native budget/root/repeated-solve tests, the existing 1,632-case feature
matrix and proof after real descriptor compaction, and 636 congruence checks.
The combined coverage assertion initially checked only vivification reductions;
elimination legitimately finished the small formulas first. Its reduction
coverage counter now includes both passes; truth/model/proof checking was
unchanged. Remote sanitizer and exact-source qualification remain separate gates.

## Prespecified full qualification

The new workflows run three independent configurations: elimination, dynamic
LBD, and their combination. Watched vivification is excluded from adoption
candidates because of its observed solve loss, but remains correctness-tested.

Each configuration gets all 30 frozen instances against the exact structural
control, with alternating arm order, 60 seconds, 4 GiB, and 300-second external
proof checking. Adoption requires the historical numerical target (9 solves,
PAR-2 below 85.12), no loss of any control solve, and aggregate paired
non-regression. Failed configurations and partial rows are retained. This does
not claim zero per-instance latency regressions or statistical superiority.

A separate full comparator lane uses unchanged pinned MiniSat and Kissat source,
and enables Kissat proofs so the strongest comparator also pays proof-output
cost. It records same-run numerical verdicts independently of adoption.
Statistical superiority remains false until repeated runs support it. No default
switch or merge is earned from a local screen or a merely green CI run.

Core correctness, comparative performance, code quality, and production readiness
remain separate verdicts. Main and all previously qualified/rejected branches
are preserved. The objective of beating Kissat remains open until certified
full-corpus evidence supports it.
