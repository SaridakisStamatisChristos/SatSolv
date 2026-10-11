# R7 assumption ledger

| ID | Statement | Criticality | Confidence | Affects | Invalidation trigger |
|---|---|---:|---:|---|---|
| A1 | C++20 is appropriate for the native high-performance solver core. | LOW | high | build/core | toolchain target changes |
| A2 | Certified mode should disable proof-unsupported preprocessing/inprocessing rather than risk unverifiable speedups. | HIGH | high | proof/search | a transformation gains verified DRAT logging |
| A3 | A RUP-only DRAT subset is sufficient for initial certification and can omit deletion records. | HIGH | high | proof logger | external drat-trim rejects valid generated traces |
| A4 | 60 s / 4 GiB is the default acceptance budget. | MEDIUM | high | benchmark | campaign brief is revised |
| A5 | SAT Competition 2023 Main Track is an acceptable public source for the fixed acceptance slice. | MEDIUM | high | benchmark | user selects a different competition/year |

No license assumption is made. The repository remains without a license until the owner makes that legal choice explicitly.
