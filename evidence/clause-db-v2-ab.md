# Clause Database v2 — certified A/B evidence

## Provenance

- Core v2 baseline: `b11f47060b99815620569e5fe2ad6b8071e5db01`
- Clause DB v2 implementation: `9b1196f4fe0c62c8ff35d5a8ca44e27a9b814fe6`
- A/B workflow head: `f54fe4ca6337c7869252d760fcc009c81b771685`
- Workflow run: `38274141909`
- Artifact: `clause-db-v2-ab-evidence` (`11758611848`)
- Artifact digest: `sha256:465d01250f5f2b9d80717b72f1e1a268817670a330fed2fe9746b37ab5f699ec`
- Corpus: 10 hard instances selected from the frozen SAT Competition 2023 manifest
- Limit: 15 seconds per solver/instance, 4 GiB, external pinned `drat-trim`

Both baseline and candidate passed certification with zero violations.

## Aggregate result

| Metric | Core v2 | Clause DB v2 | Direction |
|---|---:|---:|---:|
| Solved | 1/10 | 1/10 | tie |
| Diagnostic PAR-2 | 28.0419 | 27.5509 | 1.75% better |
| Aggregate solver seconds | 145.416 | 140.508 | 3.37% lower |
| Conflicts / second | 31,561 | 18,301 | 42.0% lower |
| Propagations / second | 3,283,406 | 2,485,004 | 24.3% lower |
| Proof bytes | 369,925,830 | 270,098,734 | 27.0% lower |

## Strongest individual signal

`clqcolor-08-06-07.shuffled-as.sat05-1257.cnf` remained certified UNSAT but improved from approximately **10.419 s** to **5.509 s**. The candidate reached the proof after roughly 256,930 conflicts versus 722,440 for Core v2.

This is evidence that the retention policy can improve search trajectory substantially on at least one hard instance. It is not evidence that the candidate has higher raw BCP throughput; the aggregate throughput counters moved in the opposite direction.

## Interpretation

The Clause DB v2 design passes the focused non-regression gate:

- no solved-count loss;
- improved diagnostic PAR-2;
- zero certification failures;
- major improvement on the one solved hard instance.

However, the lower conflicts/s and propagations/s show that retaining useful clauses has increased the cost of the active clause database and/or quality maintenance. The next decision therefore depends on the full frozen 30-instance acceptance run, not on the focused result alone.

Do not claim whole-corpus superiority or SOTA performance from this diagnostic experiment.
