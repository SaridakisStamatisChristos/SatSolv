# SatSolv

SatSolv is a native C++20 CDCL SAT solver built around a falsifiable correctness contract: SAT answers are checked by an independent model evaluator, and certified UNSAT answers carry DRAT proofs intended for independent `drat-trim` verification.

## Current status

The solver core, parser, First-UIP analysis, two-watched-literal propagation, EVSIDS-style branching, phase saving, Luby restarts, LBD metadata, conservative learnt-clause minimization/reduction, proof logging, independent model checking, exhaustive/property tests, differential-test tooling, sanitizer configuration, and resource-limited benchmark harness are implemented.

**Important:** repository presence is not certification. See `docs/r7-contract.md` and `evidence/` for evidence states. The public SAT Competition acceptance slice is not considered complete until exact instances/hashes and raw comparative results are frozen.

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
tools/verify_drat.sh /path/to/drat-trim tests/cnf/unsat_basic.cnf proof.drat
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

The benchmark runner treats a SatSolv row as solved only after independent validation.

```bash
python3 tools/benchmark.py \
  --manifest benchmarks/smoke_manifest.json \
  --root . \
  --satsolv build/satsolv \
  --drat-trim /path/to/drat-trim \
  --time-limit 10 \
  --output benchmarks/results/smoke.json
```

For acceptance, add the pinned MiniSat and Kissat binaries and use a resolved SAT Competition manifest. See `docs/benchmarking.md`.

## Design documents

- `docs/architecture.md`
- `docs/correctness.md`
- `docs/proof-system.md`
- `docs/benchmarking.md`
- `docs/r7-contract.md`
- `docs/adr-0001-language.md`

## Known limitations

The current solver intentionally avoids preprocessing/inprocessing whose proof steps are not implemented. Gzip CNF input is not yet supported. The full 30–60 instance SAT Competition acceptance campaign still requires resolved SHA-256-pinned benchmark inputs and measured SatSolv/MiniSat/Kissat results; until that evidence exists, no global performance or production-ready claim is made.
