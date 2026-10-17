# Factoring replication (two external-family sentinels)

Purpose: test whether the observed `--no-factor` regression on `or_randxor_k3_n520_m520.cnf` is unique to that instance or recurs on other Kissat-only UNSAT benchmark families.

Pinned 2026-10-18 prior observation (run 39072806782, artifact 12116345341):
- Normal Kissat 4.0.4: proof-verified UNSAT in ~1.175 s.
- `--no-congruencexors`: ~1.172 s, proof verified.
- `--no-factor`: 60 s external timeout; no definitive result.
- Normal Kissat telemetry: 7 factorization rounds; 421 reported factored variables.
- Complete read-only ternary XOR quartets: zero (does not exclude other XOR representations).

This branch reuses the **same code, pinned comparator, budgets, and independent certificate validation**, with **no new SatSolv algorithm or benchmark adoption**. Matrix inputs:
1. `posixpath__joinrealpath_13.cnf` — register-allocation UNSAT, formerly Kissat-only.
2. `mchess16-mixed-35percent-blocked.cnf` — mutilated-chessboard UNSAT, formerly Kissat-only.

Each matrix job repeats all seven options, including baseline/`--no-factor`, on a different input, under the original manifest SHA-256, seed=1, 60s, 4GiB, independent DRAT checker, per-arm raw evidence. Parallel job failures do not suppress the other evidence.

**Decision gate:** Only if disabling factoring consistently causes major regression *and* normal modes remain independently verified should a bounded proof-aware BVA/factoring prototype be specified. Lack of an effect on these two inputs means factoring is a high-value **family-specific** mechanism at most. A factorization-like transformation might involve new extension variables, requiring explicit SAT-model reconstruction and DRAT reasoning; it cannot be transplanted as a raw clause rewrite without proof obligations.
