# Mechanism fingerprint v1 — evidence before algorithm changes

**Status:** experimental diagnostic tooling, no SatSolv core changes or performance adoption.

This branch starts from the qualified `exp/satsolv-holdout-screen-v2` base commit `5aef90f3fcdf4c003adc9e7ec89928f560066cb4`, not failed dual-search PR #16.

## Question

Is the ~1 s pinned Kissat success on `or_randxor_k3_n520_m520.cnf` materially dependent on structural XOR extraction, another preprocessing family, or neither?

The frozen-30 SatSolv comparison already established 10/30 control versus 15/30 Kissat. The experimental VMTF/stable toggle in PR #16 failed adoption and must not be promoted.

## Executable discriminating experiment

`.github/workflows/mechanism-fingerprint.yml` builds the original Kissat 4.0.4 comparator at `8af8e56f174b778aef3aa45af9f739b2a5f492c2`, compiles pinned `drat-trim` `2e3b2dc0ecf938addbd779d42877b6ed69d9a985`, checks manifest SHA-256 `7ef151bdfce7cd0344e27643446f2509823b43432a64292661a6de86ceb26a53`, verifies downloaded CNF and source hashes, and invokes:

```bash
python3 tools/kissat_mechanism_screen.py \
  --manifest benchmarks/competition_manifest.json \
  --root benchmarks/cache \
  --kissat /tmp/kissat/build/kissat \
  --drat-trim /tmp/drat-trim-bin \
  --instance or_randxor_k3_n520_m520.cnf \
  --time-limit 60 --verification-time-limit 300 --memory-gib 4 \
  --output benchmarks/results/mechanism-fingerprint-v1/result.json
```

Seven arms in order: baseline, `--no-congruencexors`, `--no-congruence`, `--no-eliminate`, `--no-factor`, `--no-sweep`, and `--no-preprocess`. All get `--seed=1`, identical proof generation and resource budgets. An invalid option, crash, incorrect SAT/UNSAT, missing/invalid DRAT, or unexplained UNKNOWN is an **integrity failure**, not a bad performance score. External timeout is reported explicitly.

The harness saves a machine-readable artifact **after every arm**; the upload step executes even when a later arm fails. Results include binary/manifest/verifier SHA-256, exact commands, exit status, elapsed wall-clock time, proof verification and relevant Kissat counters.

## Read-only parity detection

The embedded CNF scanner groups exactly three-distinct-variable clauses by variable triple; it only identifies a complete four-clause truth table forbidding the same assignment parity, and ignores partial, mixed or contradictory patterns. Any detected quartet logically entails the corresponding 3-variable XOR equation; detection does not imply a general XOR extraction/solver implementation.

Exhaustive truth-table tests check both parities. Adversarial tests reject every three-of-four subset, mixed eight-clause contradictions, malformed CNF, invalid exit codes, unverified proofs, and incorrect conclusions.

## Decision boundaries

- **H1 strengthened:** many complete XOR quartets and significant slowdown under the XOR-specific ablation, ideally reproduced on multiple inputs. Then add a separate read-only detection PR before any proof-aware parity inference.
- **H1 weakened:** zero/very few quartets and negligible change in XOR ablations. Test other structural preprocessing before implementing an XOR solver.
- **H2 strengthened:** a different specific ablation causes a clear slowdown. Reconstruct that exact mechanism in isolation.
- **Ambiguous:** only broad `--no-preprocess` is much slower; run finer ablations. Do not infer XOR causality from the filename alone.
- **Search remains plausible:** all structural ablations preserve Kissat's speed. Only then prioritize coordinated search restart/mode behavior.

This single-case CI screen is **diagnostic**, not a benchmark-suite improvement claim. No algorithm is adopted and no SOTA claim is made without unchanged correctness gates, same-binary SatSolv control/candidate testing, frozen external holdouts and contemporary baselines. Neither red nor green workflow status establishes a SatSolv performance improvement by itself.
