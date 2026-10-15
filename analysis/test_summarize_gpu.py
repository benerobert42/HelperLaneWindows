import contextlib
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest

from summarize_gpu import analyze


class PairingTests(unittest.TestCase):
    def setUp(self):
        with (Path(__file__).parent / "gpu" / "smoke" / "gpu_organization.csv").open(newline="") as f:
            rows = list(csv.DictReader(f))
        self.source = next(r for r in rows if r["Method"] == "Source")
        self.method = next(r for r in rows if r["Method"] == "Heap3D")
        self.method.update({k: self.source[k] for k in ("Block", "View", "MSAA", "Shader", "Geometry")})

    def summarize(self, rows):
        with tempfile.TemporaryDirectory(dir=Path(__file__).parent / "review") as directory:
            path = Path(directory) / "cases.csv"
            with path.open("w", newline="") as f:
                writer = csv.DictWriter(f, self.source.keys())
                writer.writeheader()
                writer.writerows(rows)
            with contextlib.redirect_stdout(io.StringIO()):
                result = analyze(path)
            audit = json.loads(path.with_suffix(".audit.json").read_text())
            header = path.with_suffix(".paired.csv").read_text().splitlines()[0]
            return result, audit, header

    def test_method_only_and_empty(self):
        for rows in ([self.method], []):
            result, _, header = self.summarize(rows)
            self.assertEqual(result, [])
            self.assertIn("TimeChange_pct", header)

    def test_instances_are_part_of_pair_identity(self):
        self.method["Instances"] = str(int(self.source["Instances"]) + 1)
        result, _, _ = self.summarize([self.source, self.method])
        self.assertEqual([r["Method"] for r in result], ["Source"])

    def test_zero_helpers_and_visibility_are_distinct(self):
        self.source["DiagnosticHelpers"] = "0"
        self.method["ValidForRanking"] = "0"
        result, audit, _ = self.summarize([self.source, self.method])
        self.assertIsNone(result[0]["HelperChange_pct"])
        self.assertNotIn("ValidForRanking", next(iter(audit.values())))
        self.assertIn("MeshValid", next(iter(audit.values())))

    def test_duplicate_source_is_rejected(self):
        with self.assertRaises(ValueError):
            self.summarize([self.source, self.source])


if __name__ == "__main__":
    unittest.main()
