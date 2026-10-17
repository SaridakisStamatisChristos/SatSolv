#!/usr/bin/env python3
"""Adversarial read-only XOR gadget and result-integrity tests."""
import itertools
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from kissat_mechanism_screen import ARMS, clauses_in_dimacs, required_result, scan_xor_quartets


def gadget(forbidden_parity):
    clauses = []
    for bits in itertools.product((0, 1), repeat=3):
        if sum(bits) % 2 == forbidden_parity:
            clauses.append(tuple(-v if bit else v for v, bit in zip((1, 2, 3), bits)))
    return clauses


def satisfies(clause, bits):
    return any((bits[abs(lit) - 1] == 1) == (lit > 0) for lit in clause)


class MechanismScreenTest(unittest.TestCase):
    def scan(self, clauses, *, nvars=3):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.cnf"
            path.write_text(
                f"c truth-table gadget\np cnf {nvars} {len(clauses)}\n"
                + "".join(" ".join(map(str, cl)) + " 0\n" for cl in clauses)
            )
            return scan_xor_quartets(path)

    def test_complete_even_forbidden_is_xor_one(self):
        clauses = gadget(0)
        for bits in itertools.product((0, 1), repeat=3):
            expected = sum(bits) % 2 == 1
            self.assertEqual(all(satisfies(clause, bits) for clause in clauses), expected)
        record = self.scan(clauses)
        self.assertEqual(record["exact_xor_quartets"], 1)
        self.assertEqual(record["exact_xor_one_quartets"], 1)
        self.assertEqual(record["exact_xor_zero_quartets"], 0)

    def test_complete_odd_forbidden_is_xor_zero(self):
        clauses = gadget(1)
        for bits in itertools.product((0, 1), repeat=3):
            self.assertEqual(all(satisfies(clause, bits) for clause in clauses),
                             sum(bits) % 2 == 0)
        self.assertEqual(self.scan(clauses)["exact_xor_zero_quartets"], 1)

    def test_incomplete_and_mixed_patterns_not_inferred(self):
        four = gadget(0)
        for missing in range(4):
            self.assertEqual(self.scan(four[:missing] + four[missing + 1:])["exact_xor_quartets"], 0)
        both = gadget(0) + gadget(1)
        report = self.scan(both)
        self.assertEqual(report["exact_xor_quartets"], 0)
        self.assertEqual(report["mixed_complete_parity_patterns_excluded"], 1)

    def test_clause_order_duplicates_and_nonternary(self):
        four = [tuple(reversed(c)) for c in gadget(0)]
        report = self.scan(four + [four[0], (1, 2), (1, -1, 2), (1, 2, 3, 4)], nvars=4)
        self.assertEqual(report["exact_xor_quartets"], 1)
        self.assertEqual(report["ternary_clauses"], 5)

    def test_invalid_dimacs_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.cnf"
            for contents in ("p cnf 3 1\n1 2 3\n",
                             "p cnf 3 1\n1 2 3 0\n1 0\n",
                             "p cnf 3 1\n4 0\n",
                             "p cnf 3 1\n1 0\np cnf 3 1\n"):
                path.write_text(contents)
                with self.assertRaises(ValueError):
                    list(clauses_in_dimacs(path))

    def test_result_integrity_gate(self):
        item = {"expected": "UNSAT"}
        baseline = {"timed_out": False, "crashed": False, "exit_code": 20,
                    "result": "UNSAT", "verified": True}
        self.assertIsNone(required_result(item, baseline))
        self.assertIsNotNone(required_result(item, dict(baseline, verified=False)))
        self.assertIsNotNone(required_result(item, dict(baseline, result="SAT", exit_code=10)))
        self.assertIsNotNone(required_result(item, dict(baseline, result="UNKNOWN", exit_code=1)))
        self.assertIsNone(required_result(item, dict(baseline, timed_out=True,
                                                    result="TIMEOUT", exit_code=None)))
        self.assertEqual(len(ARMS), 7)
        self.assertEqual(ARMS[0], ("baseline", ()))


if __name__ == "__main__":
    unittest.main()
