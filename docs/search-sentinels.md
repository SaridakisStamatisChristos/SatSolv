# Search Sentinel v1 — separate variable selection from switch cadence

**Status:** exploratory experiment layered on the unchanged, failed-adoption Dual-Search v1 branch. No solver behavior is enabled by default or adopted.

## Design

Five SatSolv arms use the exact same one-time Release build (identical SHA-256):

| Arm | Additional flags beyond common control |
|---|---|
| control | none (existing delayed target-phase/EVSIDS) |
| candidate | `--experimental-dual-search --dual-search-interval 1000` |
| focus_only | `--experimental-dual-search --dual-search-interval 1000000000000` |
| switch_10k | `--experimental-dual-search --dual-search-interval 10000` |
| switch_100k | `--experimental-dual-search --dual-search-interval 100000` |

All include `--experimental-target-phases --search-warmup 1000000`, seed 1, 60 s, 4 GiB and independent SAT model / UNSAT DRAT checks.

The four sentinels from the source-sha-pinned 30-instance competition slice are:
- `vmpc_28.shuffled-as.sat05-1957.cnf`: v1 lost the control's easy SAT.
- `stb_792_333.apx_1_DS-ST.cnf`: v1 found SAT that control did not.
- `ibm-2004-03-k70.cnf`: v1 and control both SAT, but runtime differed.
- `clqcolor-08-06-07.shuffled-as.sat05-1257.cnf`: both UNSAT but v1 was slower.

The selection is diagnostic and intentionally *biased by previously observed results*. It cannot establish general speedups, statistical superiority, or the 30-instance adoption gate.

## Reproduction

```bash
python3 tools/search_sentinel_ab.py prepare \
  --manifest benchmarks/competition_manifest.json \
  --binary build/satsolv --source-commit "$(git rev-parse HEAD)" \
  --output benchmarks/results/search-sentinels-v1
python3 tools/paired_campaign.py \
  --manifest benchmarks/results/search-sentinels-v1/sentinel_manifest.json \
  --root benchmarks/cache \
  --config benchmarks/results/search-sentinels-v1/config.json \
  --drat-trim /tmp/drat-trim-bin --seed 1 \
  --time-limit 60 --verification-time-limit 300 --memory-gib 4 \
  --minimum-solves 0 \
  --output benchmarks/results/search-sentinels-v1/result.json
python3 tools/search_sentinel_ab.py audit \
  --result benchmarks/results/search-sentinels-v1/result.json \
  --output benchmarks/results/search-sentinels-v1/audit.json
```

The PR workflow executes these commands with frozen source/manifest/verifier hashes; data uploaded even on failure.

## Falsification

- If focus-only reproduces `vmpc` loss, VMTF variable ordering is sufficient to trigger that loss; interval tuning alone is not the complete repair.
- If focus-only preserves `vmpc`, but 1k-switch loses it, mode switching and its interaction with phase/restarter state become primary suspects.
- If 10k/100k preserve the win on `stb` and avoid losing `vmpc`, expand to original full-30 with prespecified adoption gate before suggesting any mode schedule.
- If no policy beats control on both asymmetrical sentinels, reject further switching-interval sweeps and investigate mode-specific restarts/phase policies separately.

The existing Luby restart, clause DB, proof behavior, and target-phase semantics are unchanged. These are four selected examples and thus cannot authorize a merge into any qualified baseline.
