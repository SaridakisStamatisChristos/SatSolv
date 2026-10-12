#!/usr/bin/env python3
"""Regression test: a SatSolv internal-error exit must make the benchmark fail closed."""
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
        cnf = root / "tiny.cnf"
        cnf.write_text("p cnf 1 1\n1 0\n")
        digest = hashlib.sha256(cnf.read_bytes()).hexdigest()
        manifest = root / "manifest.json"
        manifest.write_text(
            json.dumps(
                {
                    "instances": [
                        {
                            "name": "synthetic-internal-error",
                            "path": "tiny.cnf",
                            "sha256": digest,
                            "expected": "SAT",
                        }
                    ]
                }
            )
        )

        fake_solver = root / "fake-satsolv"
        write_executable(
            fake_solver,
            """#!/usr/bin/env python3
import sys
if '--version' in sys.argv or '-version' in sys.argv:
    print('fake-satsolv internal-error regression')
    raise SystemExit(0)
print('c INTERNAL_ERROR: std::bad_alloc', file=sys.stderr)
raise SystemExit(3)
""",
        )
        fake_drat = root / "fake-drat-trim"
        write_executable(
            fake_drat,
            """#!/usr/bin/env python3
if '--version' in sys.argv or '-version' in sys.argv:
    print('fake-drat-trim')
raise SystemExit(0)
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
            raise SystemExit("benchmark did not preserve JSON evidence on solver error")

        evidence = json.loads(output.read_text())
        if evidence.get("schema") != 4:
            raise SystemExit(f"unexpected benchmark schema: {evidence.get('schema')!r}")
        certification = evidence.get("certification", {})
        if certification.get("passed") is not False:
            raise SystemExit("certification unexpectedly passed")
        violations = certification.get("violations", [])
        matches = [v for v in violations if v.get("kind") == "solver_error"]
        if len(matches) != 1 or matches[0].get("exit_code") != 3:
            raise SystemExit(f"missing structured solver_error violation: {violations!r}")
        row = evidence["rows"][0]["solvers"]["satsolv"]
        if row.get("exit_code") != 3 or row.get("result") != "UNKNOWN":
            raise SystemExit(f"unexpected synthetic row: {row!r}")
        if evidence["summary"]["satsolv"].get("solver_errors") != 1:
            raise SystemExit(f"solver error not counted in summary: {evidence['summary']['satsolv']!r}")

    print("internal-error fail-closed benchmark regression passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
