# Benchmarking

`tools/benchmark.py` is the fail-closed evidence harness for SatSolv. It enforces an external wall-clock timeout, an address-space memory limit on POSIX, and preserves command lines, exit status, raw stdout, raw stderr, timing, verification transcripts, proof size, tool versions, repository commit, and manifest hash in JSON evidence.

For SatSolv results:

- `SAT` is counted only after independent validation by `tools/check_model.py`.
- `UNSAT` is counted only after external `drat-trim` emits the exact verdict `s VERIFIED`.
- `UNKNOWN` and an external timeout are non-solves, not failures of logical soundness.
- A crash or any non-timeout SatSolv exit outside `{0, 10, 20}` is a structured certification violation. In particular, exit code `3` (`INTERNAL_ERROR`) is recorded as `solver_error`; it cannot be hidden as ordinary UNKNOWN.
- An invalid SAT model, missing/rejected/timed-out UNSAT proof, result contradicting the frozen manifest, or baseline result contradicting the manifest is also a certification violation.
- The raw JSON is written before the harness exits non-zero so failed acceptance runs retain forensic evidence.

Current benchmark evidence schema: **4**. A passing acceptance artifact requires `certification.passed=true`, an empty `certification.violations` list, zero SatSolv verification failures, zero SatSolv crashes, and zero SatSolv solver errors.

Pinned comparison baselines:

- Kissat 4.0.2 commit `f7e57764c19cf35198830575492dc9d80e872b55`.
- MiniSat 2.2.0 commit `eb01ad68b75bb3b34ff8657c37ad6a31faae0fc3`; built as its optimized `minisat_release` target with `g++ -fpermissive` solely for compatibility with modern GCC friend-declaration diagnostics. Upstream source remains unchanged.
- drat-trim commit `2e3b2dc0ecf938addbd779d42877b6ed69d9a985`.

The committed smoke manifest is for CI engineering only. The acceptance corpus is the resolved `benchmarks/competition_manifest.json`: **30 public SAT Competition 2023 instances** frozen by source URL plus compressed/decompressed SHA-256. `tools/fetch_benchmarks.py` must successfully fetch and hash-check every input before comparison begins.

## Resource policy

Competitive solver budget:

- **60 s wall clock per solver/instance**.
- **4 GiB address-space limit per solver process** on the Linux acceptance runner.

Independent proof-verification budget:

- **300 s per SatSolv UNSAT proof** for `drat-trim`.

The proof-checking budget is intentionally independent of the competitive solver budget. A solver must produce its UNSAT answer inside 60 s; certification may then consume up to 300 s. Checker time is not credited as solver performance.

Summary metrics include solved count, timeouts, crashes, solver errors, verification failures, median solved time, geometric mean solved time, and PAR-2. Raw per-instance rows are authoritative; a green Actions badge by itself is not acceptance evidence.

`tools/report_benchmark.py` renders the per-instance and aggregate Markdown tables directly from raw JSON. Human-readable reports are therefore derived artifacts rather than manually entered results.
