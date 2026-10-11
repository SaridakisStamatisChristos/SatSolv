# Benchmarking

`tools/benchmark.py` enforces an external wall-clock timeout, an address-space memory limit on POSIX, preserves commands, exit status, raw stdout, and raw stderr in its JSON evidence, validates SatSolv SAT models with `tools/check_model.py`, and validates SatSolv UNSAT proofs with external `drat-trim`. It never counts UNKNOWN, timeout, crash, invalid model, missing proof, or rejected proof as a SatSolv solve.

Pinned comparison baselines:

- Kissat 4.0.2 commit `f7e57764c19cf35198830575492dc9d80e872b55`.
- MiniSat 2.2.0 commit `eb01ad68b75bb3b34ff8657c37ad6a31faae0fc3`.
- drat-trim commit `2e3b2dc0ecf938addbd779d42877b6ed69d9a985`.

The committed smoke manifest is for CI engineering only. The full acceptance benchmark is required to freeze 30–60 public SAT Competition instances by source URL and SHA-256 before results are accepted. `benchmarks/competition_selection.json` pins the official SAT Competition 2023 Main Track source and resource policy but is explicitly **not** claimed as resolved acceptance evidence yet.

Default acceptance budget: 60 s wall clock and 4 GiB per solver/instance. Deep runs may use 300 s. Summary metrics include solved count, timeouts, crashes, median solved time, geometric mean solved time, and PAR-2; raw per-instance rows are retained.

`tools/report_benchmark.py` renders the required per-instance and aggregate Markdown tables directly from that raw JSON. Human-readable reports are therefore derived artifacts rather than manually entered results.
