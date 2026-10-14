# SatSolv

SatSolv is a native C++20 CDCL SAT solver built around a falsifiable correctness contract: SAT answers are checked by an independent model evaluator, and certified UNSAT answers carry DRAT proofs for independent `drat-trim` verification.

## Current status

The solver core, parser, First-UIP analysis, two-watched-literal propagation, EVSIDS-style branching, phase saving, Luby restarts, LBD metadata, conservative learnt-clause minimization/reduction, proof logging, independent model checking, exhaustive/property tests, differential-test tooling, sanitizer configuration, and resource-limited benchmark harness are implemented.

The acceptance corpus is a resolved, SHA-256-pinned **30-instance SAT Competition 2023 slice**. Pinned MiniSat, Kissat, and `drat-trim` comparisons are operational. A repaired full-corpus artifact has independently validated every SatSolv SAT/UNSAT answer it counted, including a 38,155,990-byte UNSAT DRAT proof. Subsequent raw-evidence review exposed five exit-code-3 `std::bad_alloc` cases that the older evidence schema had treated as ordinary UNKNOWN. The solver memory path and benchmark semantics have since been repaired; focused 4 GiB regression probes and CI are green, while the final schema-4 full-corpus revalidation remains an explicit evidence gate.

**Important:** repository presence and a green Actions badge are not certification. See `docs/r7-contract.md` and `evidence/failure-repair-ledger.md` for the current evidence states and falsification history. No production-ready or global-performance claim is made while a hard R7 requirement remains `REVERIFYING`.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## CLI

```bash
./build/satsolv instance.cnf
./build/satsolv instance.cnf --proof instance.drat
./build/satsolv instance.cnf --seed 1
./build/satsolv instance.cnf --time-limit 60
./build/satsolv instance.cnf --stats
./build/satsolv instance.cnf --stats-json stats.json
```

Exit codes follow SAT Competition convention where practical: `10` SAT, `20` UNSAT, `0` UNKNOWN, `2` input/usage error, `3` internal error.

## Independent SAT validation

```bash
./build/satsolv tests/cnf/sat_basic.cnf > result.out || test $? -eq 10
python3 tools/check_model.py tests/cnf/sat_basic.cnf result.out
```

## DRAT certification

Build upstream `drat-trim` at the pinned commit documented in `docs/proof-system.md`, then:

```bash
./build/satsolv tests/cnf/unsat_basic.cnf --proof proof.drat || test $? -eq 20
bash tools/verify_drat.sh /path/to/drat-trim tests/cnf/unsat_basic.cnf proof.drat
```

CI performs this with the external checker; `tools/check_rup.py` is only an additional development oracle.

## Strong local correctness checks

```bash
python3 tools/exhaustive_check.py build/satsolv --random 500
python3 tools/metamorphic_check.py build/satsolv --cases 300
python3 tools/parser_adversarial.py build/satsolv
```

## Sanitizers

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSATSOLV_ENABLE_SANITIZERS=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
python3 tools/exhaustive_check.py build-asan/satsolv --random 100
```

## Benchmark harness

The benchmark runner is fail closed. It counts a SatSolv row as solved only after independent validation, and it treats a non-timeout exit outside `{0,10,20}` as a certification failure rather than hiding it as UNKNOWN.

```bash
python3 tools/benchmark.py \
  --manifest benchmarks/competition_manifest.json \
  --root benchmarks/cache \
  --satsolv build/satsolv \
  --minisat /path/to/minisat \
  --kissat /path/to/kissat \
  --drat-trim /path/to/drat-trim \
  --time-limit 60 \
  --verification-time-limit 300 \
  --memory-gib 4 \
  --output benchmarks/results/acceptance.json
python3 tools/report_benchmark.py benchmarks/results/acceptance.json benchmarks/results/acceptance.md
```

Raw schema-4 JSON retains solver commands/stdout/stderr, exit codes, resource outcomes, proof/model verification transcripts, repository/tool versions, and manifest provenance. Human-readable Markdown is generated from that raw evidence. See `docs/benchmarking.md`.

## Design documents

- `docs/architecture.md`
- `docs/correctness.md`
- `docs/proof-system.md`
- `docs/benchmarking.md`
- `docs/r7-contract.md`
- `docs/adr-0001-language.md`

## Known limitations

The current solver intentionally avoids preprocessing/inprocessing whose proof steps are not implemented. Gzip CNF input is not yet supported. SatSolv is materially behind Kissat on solved-count coverage in the current public benchmark evidence, so performance engineering remains active even though independently checked SAT/UNSAT soundness evidence is strong. Final R7 acceptance additionally requires the hardened schema-4 full-corpus run to close without solver errors, crashes, or verification failures.


## Core v3 candidate

This branch starts from exact Core v2 and implements blocker-first propagation,
indexed EVSIDS, active learned IDs and safe descriptor remapping, buffered binary
DRAT with deletions, seed diversification, and bounded proof-safe simplification
and binary minimization. See [design and invariants](docs/core-v3.md) and
[executed local checks](evidence/core-v3-local.md). Full-corpus performance and
remote qualification remain required before adoption; no SOTA claim is made.

## Structural repair candidate

The default now uses the stronger observed engine-only configuration plus a
proof-safe binary-SCC/complete-AND congruence pass. Earlier search mechanisms
remain available through explicit experimental flags. See
[comparative audit, invariants and executed checks](docs/structural-repair.md).
Frozen full-corpus paired qualification and comparator runs are required before
adoption. Main is unchanged; no global superiority claim is made.
