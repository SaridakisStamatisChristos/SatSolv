#!/usr/bin/env python3
"""Minimum counterexamples for exploratory same-binary mode-sentinel tooling."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import search_sentinel_ab as screen


class SentinelConfigTest(unittest.TestCase):
    def test_fixed_and_near_focused_only_are_distinct(self):
        self.assertEqual(screen.expected_flags(None), screen.CONTROL)
        self.assertEqual(screen.expected_flags(1000)[-2:], ["--dual-search-interval", "1000"])
        self.assertEqual(screen.expected_flags(1000000000000)[-1], "1000000000000")
        self.assertEqual(len({name for name, _ in screen.VARIANTS}), len(screen.VARIANTS))

    def test_prepare_fails_if_any_sentinel_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "satsolv"
            binary.write_bytes(b"test")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"instances": [
                {"name": screen.SENTINELS[0], "path": "x", "sha256": "abc", "expected": "SAT"}
            ]}))
            with self.assertRaises(SystemExit):
                screen.prepare(SimpleNamespace(manifest=manifest, binary=binary,
                                               source_commit="abc", output=root / "out"))

    def test_prepare_freezes_parent_manifest_and_arms(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "satsolv"
            binary.write_bytes(b"test")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"instances": [
                {"name": name, "path": name, "sha256": "hash", "expected": "UNSAT"}
                for name in reversed(screen.SENTINELS)
            ]}))
            output = root / "results"
            screen.prepare(SimpleNamespace(manifest=manifest, binary=binary,
                                           source_commit="sha", output=output))
            selected = json.loads((output / "sentinel_manifest.json").read_text())
            config = json.loads((output / "config.json").read_text())
            self.assertEqual([x["name"] for x in selected["instances"]], list(screen.SENTINELS))
            self.assertEqual([x["name"] for x in config["arms"]],
                             [name for name, _ in screen.VARIANTS])
            self.assertEqual(config["source_manifest_sha256"], screen.benchmark.sha256(manifest))
            self.assertEqual(config["manifest_sha256"],
                             screen.benchmark.sha256(output / "sentinel_manifest.json"))
            self.assertEqual(len({x["binary"] for x in config["arms"]}), 1)
            self.assertEqual(config["arms"][0]["flags"], screen.CONTROL)


if __name__ == "__main__":
    unittest.main()
