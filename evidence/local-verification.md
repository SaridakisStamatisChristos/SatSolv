# Local verification evidence

Environment observed during the R7 working-tree run:

- compiler: g++ 14.2.0;
- CMake: 3.31.6;
- Python: 3.13.5;
- platform: Linux container supplied by the execution environment.

Observed release gates:

- strict `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` build: PASS;
- native CTest suite: 1/1 PASS;
- `tools/exhaustive_check.py --random 500`: PASS, 756 formulas;
- `tools/metamorphic_check.py --cases 300`: PASS, 300 cases × 4 transformations;
- `tools/parser_adversarial.py`: PASS, 9 malformed-input cases;
- Python tool syntax compilation: PASS.

Observed sanitizer gates:

- ASan + UBSan build: PASS;
- native CTest under sanitizers: 1/1 PASS;
- sanitizer exhaustive/random run: PASS, 356 formulas.

Observed deep proof/search stress after minimizer repair:

- pigeonhole 8 pigeons / 7 holes: UNSAT;
- conflicts: 4,121;
- decisions: 4,616;
- propagations: 58,503;
- restarts: 20;
- learnt clauses: 4,120;
- deleted learnt clauses: 2,497;
- development proof oracle: PASS for all 4,121 RUP additions including the empty clause.

Evidence boundary: upstream `drat-trim`, MiniSat, and Kissat were not installed in the local container and outbound git access was unavailable. Their acceptance evidence must therefore come from the pushed GitHub Actions revision; they are not marked locally verified.
