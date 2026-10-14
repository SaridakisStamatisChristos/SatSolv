# Structure-preserving strengthening and exact-winner confirmation

This experiment extends Core v2's native CDCL engine from immutable PR9 source
`9a44a9b2e550f50c5364089dd2b67870cee109c6`. It does not establish SOTA.
The strongest previous frozen-30 observation is 10 certified solves from delayed
target phases; tested comparator arms yielded 15–16 for Kissat 4.0.2. The exact
target-only winner was not previously in the full comparator matrix.

## Mechanisms and hard invariants

`--experimental-short-ssr` performs bounded self-subsuming resolution using
indexed short supports, up to two passes. If `(not p or D)` exists and `D` is a
subset of `C`, replace `(p or C)` with `C`. Short supports have at most eight
literals; targets at most 32. Stale occurrence entries are revalidated against
the current support. At most 128 supports are examined for a target pivot.

`--experimental-implication-strengthen` traverses the binary implication graph.
For a clause containing `a` and `b`, a path `a -> b` permits removal of `a`.
A contradictory implication closure under `a` proves the unit `not a`. Each
closure inspects at most 128 edges; incomplete traversals can certify only paths
or contradictions actually reached. Every new binary strengthens the graph.

Both preserve every variable and every satisfying assignment. Each replacement
is emitted as a RUP addition **before** the supporting working clause is
replaced. No proof deletion, algebraic certificate, external solver backend,
truth-based solver selection, or variable reconstruction is introduced.
Existing original-CNF SAT validation and independent DRAT verification remain
required. Implication edges from old clauses stay sound after strengthening:
their stronger replacements still imply the old clauses.

All switches are off by default. `--strengthen-budget` bounds the total charged
work, default eight million. In the combined arm, half is reserved for graph
reasoning so resolution cannot starve it. Additional workspace is declined above
200,000 variables, one million clauses, or two million original literals.
If no strengthening fires, the original input/order is used. A zero budget must
preserve the entire result/model/proof trace.

`--strengthen-keep-activity` retains occurrence-based initial priorities from the
input to strengthening. It is an explicit causal ablation: stronger propagation
can otherwise become entangled with a changed initial variable ranking. VSIDS
updates during search remain unchanged.

`--experimental-root-probe` uses the existing watched propagation engine at
root level, without copying the clause database. Each temporary assumption
can yield a failed-literal unit or up to eight new binary implications. Every
addition is RUP-logged before entering the learned database. Temporary trails,
reason locks and saved phases are restored before permanent additions. A
partially propagated trail can justify only consequences actually reached.
Each round examines at most 128 candidate variables and 256 polarities, with
one million charged propagation/watch/deduplication steps by default. Its
default activation is the first restart after one million conflicts, followed
by increasing conflict-based intervals. This preserves the known short
regression paths but is selected using the frozen corpus, so a family holdout
is still required. `--probe-start 0` exercises startup probing in correctness
checks; `--probe-budget 0` must be byte-identical to disabling probing.

## Evidence graph and competing explanations

| Claim | State / evidence | Discriminator |
|---|---|---|
| Delayed target phases improve this solver | Tentative: one certified 10/30 paired observation, 3.77% lower PAR-2 | Three exact-source, same-seed full-30 repetitions |
| Smaller clauses necessarily improve search | Contradicted by local VMPC screen: 1,423 removals but 113,926 -> 513,870 conflicts | Preserve initial priorities and compare separately |
| Changed initial priorities alone explain that regression | Contradicted as a sufficient explanation: kept-priority VMPC still misses 30 seconds | Test watched probing as a separate mechanism |
| Stronger binary implications can help the crypto family | Hypothetical; local graph arm gains crypto versus one timed-out control but slows VMPC | Full-30 paired comparison, no lost solves, replication |
| Wall-clock activation is a stable policy | Unknown; unchanged control's crypto outcome varies in local screens | Two prespecified conflict-only activation repetitions |
| SatSolv solves more than current Kissat | Unestablished | Matched proof-enabled Kissat 4.0.4 comparisons |

The local initial screen contains 18 observations on three instances at 30 seconds
and is preserved in `docs/evidence/structural-search/initial-local-screens.json`.
Its initial shared-budget combined arm is superseded by the explicit reserved
graph budget. The first VMPC pair overlapped inherited-policy certification;
local timing is exploratory, not the authoritative qualification result.
No original frozen-30 input, expected truth, resource limit or historical result
has been changed.

Self-subsuming resolution and implication-based strengthening are prior art,
not a novelty claim. Primary background:
[SAT Handbook preprocessing chapter](https://cca.informatik.uni-freiburg.de/papers/BiereJarvisaloKiesl-SAT-Handbook-2021-Preprocessing-Chapter-Manuscript.pdf),
[Unhiding Redundancy in SAT](https://cca.informatik.uni-freiburg.de/biere/talks/Biere-Ringberg11.pdf),
[Chasing Target Phases](https://fmv.jku.at/papers/BiereFleury-POS20.pdf).

## Verification and declared full-30 campaign

The independent native transformation test evaluates every assignment of each
of 6,000 bounded random transformation cases, and checks every emitted addition
using a separate whole-formula RUP implementation. `strengthen_check.py` covers
original truth, original-CNF models, independent RUP, external DRAT,
twelve configurations, two seeds, budget boundaries, repeated preprocessing interactions,
congruence enabled/disabled and zero-budget byte identity.

`.github/workflows/structural-search.yml` gates all timing jobs on release tests,
external proof checks, inherited policy/real-compaction checks and leak-enabled
ASan/UBSan. Ten jobs are fixed before the full-30 results:

1. SSR over the exact prior target-only winner.
2. Binary implication strengthening over that winner.
3. Combined strengthening over that winner, with reserved graph budget.
4. SSR with preserved initial priorities over that winner.
5. Watched root probing over that winner.
6–8. Three exact-source target-only repetitions versus the structural control.
9–10. Two exact-source conflict-only target activations versus that control.

Winner source is pinned to `9a44a9b2e550f50c5364089dd2b67870cee109c6`;
structural control to `f997df1bec87f045ffa48d16ab50729cd90e9a0e`;
Kissat 4.0.4 to `8af8e56f174b778aef3aa45af9f739b2a5f492c2`;
drat-trim to `2e3b2dc0ecf938addbd779d42877b6ed69d9a985`.
The historical Kissat 4.0.2 evidence remains a separate lane.

Each job uses the unchanged ordered 30-instance manifest SHA-256
`7ef151bdfce7cd0344e27643446f2509823b43432a64292661a6de86ceb26a53`,
60 solver seconds, four GiB, 300 proof-verification seconds, seed 1, and balanced
rotating/reversed execution order. Timing includes parsing and preprocessing.
Every completed observation is checkpointed with invocation, executable digest,
source pin, original input hash, statistics, witness/checker log and exit code.

The paired gate requires at least ten certified solves, no lost control solve,
at least as many solves as the paired control, and no worse paired PAR-2.
Strict numerical Kissat superiority separately requires **more** certified solves
and lower PAR-2. A green execution/paired gate is not a Kissat superiority claim.
No statistical or general SOTA claim is inferred from these numerical gates.
Disjoint-family holdout remains required before broader adoption or SOTA claims.

## Remaining architectural gaps

These changes target a missing cheap inference layer. They do not reproduce
Kissat's complete preprocessing/inprocessing/search architecture. Earlier BVE,
legacy dynamic-LBD and immediate policy failures remain rejected. Symmetry and
hidden parity require separately qualified proof-generating implementations;
the current DRAT contract must not be bypassed to manufacture solver gains.
