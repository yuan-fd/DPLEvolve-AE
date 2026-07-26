import csv
import os
import subprocess
import sys
import tempfile
import unittest
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ArianeDiagnosticTests(unittest.TestCase):
    def test_dry_run_does_not_write_to_the_experiment_state_root(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            forbidden_state = Path(temp_dir) / "must-not-be-created"
            env = os.environ.copy()
            env["DPL_EVOLVE_STATE_ROOT"] = str(forbidden_state)
            result = subprocess.run(
                [
                    "bash",
                    str(ROOT / "scripts/reproduce/reproduce_ariane_diagnostic.sh"),
                    "--dry-run",
                    "--label",
                    "guided_iter_01",
                    "--threads",
                    "2",
                ],
                cwd=ROOT,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=True,
            )
            self.assertIn("partial/dry diagnostic complete", result.stdout)
            self.assertFalse(forbidden_state.exists())

    def test_six_sources_and_retained_group_means_are_verified(self):
        retained_root = ROOT / "artifacts/01-table4-qor/inputs/diagnostics"
        result = subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/reproduce/summarize_ariane_diagnostic.py"),
                "--config",
                str(ROOT / "configs/reproduction/ariane-diagnostic.tsv"),
                "--source-root",
                str(ROOT / "artifacts/01-table4-qor/diagnostics/ariane-warmstart/programs"),
                "--check-sources",
                "--retained-tsv",
                str(retained_root / "ariane133_warmstart_smoke_diagnostic.tsv"),
                "--retained-manifest",
                str(retained_root / "MANIFEST.sha256"),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
        )
        self.assertIn("six Ariane diagnostic source trees", result.stdout)
        self.assertIn("group means verified", result.stdout)

        with (retained_root / "ariane133_warmstart_smoke_diagnostic.tsv").open(newline="") as stream:
            rows = [
                row for row in csv.DictReader(stream, delimiter="\t")
                if row["iteration"] != "mean"
            ]
        values = defaultdict(list)
        for row in rows:
            values[row["group"]].append(
                (
                    float(row["delta_hpwl_vs_default_percent"]),
                    float(row["runtime_ratio_vs_default"]),
                )
            )
        expected = {
            "missed_handoff_sourceTopK": (1.5165369894134857, 1.0667049349977362),
            "level1_guided_handoff": (-3.2595354540577093, 0.7063805704676281),
        }
        for group, pair in expected.items():
            actual = (
                sum(value[0] for value in values[group]) / len(values[group]),
                sum(value[1] for value in values[group]) / len(values[group]),
            )
            self.assertAlmostEqual(actual[0], pair[0], places=12)
            self.assertAlmostEqual(actual[1], pair[1], places=12)


if __name__ == "__main__":
    unittest.main()
