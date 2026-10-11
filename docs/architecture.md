# Architecture

SatSolv is a single-process CDCL solver. Literals use signed DIMACS integers. Assignments, levels, reasons, phase state, and EVSIDS activity are dense vectors indexed by variable. Clauses are stored by stable integer ID. Each non-unit clause owns two watch positions; watch lists map a literal to clause IDs currently watching that literal. Deletion is logical (`deleted=true`) so reason IDs never dangle.

The trail is append-only between backtracks. `trail_lim` stores the starting trail offset for each decision level. Propagation scans clauses watching the negation of each newly true trail literal, relocates a false watch when possible, enqueues the other watch when unit, or reports conflict.

Conflict analysis uses First UIP resolution over reason clauses. The learnt clause's first literal is asserting after non-chronological backtracking; its second position is normalized to the highest remaining decision level. A conservative resolution-subset minimizer removes a literal only when its complete reason side is already present in the learnt clause. EVSIDS-style variable activity, phase saving, deterministic tie breaking, Luby restarts, LBD metadata, and conservative learnt-database reduction provide the baseline search policy.

Proof mode emits each learnt clause as a DRAT addition. SatSolv emits a RUP-only subset of DRAT and omits deletion records; retaining old clauses in a checker is sound. Root contradiction emits the empty clause. Unsupported preprocessing/inprocessing is intentionally disabled in certified mode.
