#!/usr/bin/env python3
"""Regression test: an unverified UNSAT must make the benchmark harness fail closed."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def write_executable(path: Path, text: str) -> None:
    path.write_text(text)
    path.chmod(path.stat().st_mode | 0o111)


def main() -> int:
    tools = Path(__file__).resolve().parent
    benchmark = tools / "benchmark.py"

    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        cnf = root / "unsat.cnf"
        cnf.write_text("p cnf 1 2\n1 0\n-1 0\n")
        digest = hashlib.sha256(cnf.read_bytes()).hexdigest()

        manifest = root / "manifest.json"
        manifest.write_text(
            json.dumps(
                {
                    "instances": [
                        {
                            "name": "deliberately-unverified-unsat",
                            "path": "unsat.cnf",
                            "sha256": digest,
                            "expected": "UNSAT",
                        }
                    ]
                }
            )
        )

        fake_solver = root / "fake-satsolv"
        write_executable(
            fake_solver,
            """#!/usr/bin/env python3
import json
from pathlib import Path
import sys
args = sys.argv[1:]
proof = Path(args[args.index('--proof') + 1])
stats = Path(args[args.index('--stats-json') + 1])
proof.write_text('1 0\\n0\\n')
stats.write_text(json.dumps({'synthetic': True}))
print('s UNSATISFIABLE')
raise SystemExit(20)
""",
        )

        fake_drat = root / "fake-drat-trim"
        write_executable(
            fake_drat,
            """#!/usr/bin/env python3
print('c deliberately rejecting proof')
print('s NOT VERIFIED')
raise SystemExit(1)
""",
        )

        output = root / "result.json"
        proc = subprocess.run(
            [
                sys.executable,
                str(benchmark),
                "--manifest",
                str(manifest),
                "--root",
                str(root),
                "--satsolv",
                str(fake_solver),
                "--drat-trim",
                str(fake_drat),
                "--time-limit",
                "2",
                "--verification-time-limit",
                "2",
                "--memory-gib",
                "1",
                "--output",
                str(output),
            ],
            capture_output=True,
            text=True,
            env=os.environ.copy(),
            timeout=15,
        )

        if proc.returncode != 3:
            print(proc.stdout)
            print(proc.stderr, file=sys.stderr)
            raise SystemExit(f"expected benchmark exit 3, got {proc.returncode}")
        if not output.exists():
            raise SystemExit("benchmark did not preserve JSON evidence on certification failure")

        evidence = json.loads(output.read_text())
        certification = evidence.get("certification", {})
        if certification.get("passed") is not False:
            raise SystemExit("certification unexpectedly passed")
        violations = certification.get("violations", [])
        if not any(v.get("kind") == "verification_failure" for v in violations):
            raise SystemExit(f"missing verification_failure violation: {violations!r}")
        row = evidence["rows"][0]["solvers"]["satsolv"]
        if row.get("result") != "UNSAT" or row.get("verified") is not False:
            raise SystemExit(f"unexpected synthetic row: {row!r}")

    print("fail-closed benchmark certification regression passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
