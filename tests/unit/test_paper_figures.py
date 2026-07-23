import csv
import json
import subprocess
import sys
import tempfile
import unittest
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/reproduce/reproduce_figures.py"


class PaperFigureTests(unittest.TestCase):
    def reproduce(self, figure: str, output: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                figure,
                "--source",
                "retained",
                "--artifact-root",
                str(ROOT),
                "--state-root",
                str(output / "unused-state"),
                "--output-dir",
                str(output),
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
        )

    def test_figure4_retained_log_is_not_silently_imputed(self):
        expected = json.loads(
            (ROOT / "artifacts/01-table4-qor/expected/table4.json").read_text()
        )["cases"]
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            result = self.reproduce("figure4", output)
            self.assertIn("verified 3 retained", result.stdout)
            with (output / "figure4-best-so-far.tsv").open(newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(rows), 96)
            grouped = defaultdict(list)
            for row in rows:
                grouped[row["case"]].append(row)
                self.assertNotEqual(row["best_so_far_delta_hpwl_percent"], "")
            self.assertEqual(len(grouped), 9)
            for case, series in grouped.items():
                expected_iterations = list(range(11))
                if case == "swerv_wrapper_asap7":
                    expected_iterations = list(range(9))
                elif case == "swerv_wrapper_nangate45":
                    expected_iterations = list(range(10))
                self.assertEqual([int(row["iteration"]) for row in series], expected_iterations)
                values = [float(row["best_so_far_delta_hpwl_percent"]) for row in series]
                self.assertTrue(
                    all(next_value <= value + 1e-12 for value, next_value in zip(values, values[1:]))
                )
                self.assertAlmostEqual(values[-1], expected[case]["hpwl_delta"], delta=0.01)
                self.assertTrue(all(row["point_status"] == "observed" for row in series))
            missing = json.loads(
                (output / "figure4-missing-points.json").read_text(encoding="utf-8")
            )["missing_points"]
            self.assertEqual(
                {(row["case"], row["iteration"]) for row in missing},
                {
                    ("swerv_wrapper_asap7", 9),
                    ("swerv_wrapper_asap7", 10),
                    ("swerv_wrapper_nangate45", 10),
                },
            )
            self.assertTrue((output / "figure4-best-so-far.svg").is_file())

    def test_figure5_uses_runtime_ratio_and_exact_bo_populations(self):
        retained = (
            ROOT / "artifacts/01-table4-qor/inputs/figures/bo_evolve_pareto_points.tsv"
        )
        with retained.open(newline="") as stream:
            retained_first = next(csv.DictReader(stream, delimiter="\t"))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            self.reproduce("figure5", output)
            with (output / "figure5-runtime-quality.tsv").open(newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            first = rows[0]
            self.assertAlmostEqual(
                float(first["runtime_ratio"]), float(retained_first["runtime_ratio"]), places=9
            )
            expected_best = {
                ("aes_nangate45", "OpenROAD BO"): -1.197715,
                ("aes_nangate45", "OpenROAD Evolve"): -3.668917,
                ("ariane133_nangate45", "OpenROAD BO"): -0.064931,
                ("ariane133_nangate45", "OpenROAD Evolve"): -5.499707,
            }
            for case in ("aes_nangate45", "ariane133_nangate45"):
                bo_rows = [
                    row for row in rows
                    if row["case"] == case and row["method"] == "OpenROAD BO"
                ]
                self.assertEqual(len(bo_rows), 400)
            for key, expected in expected_best.items():
                values = [
                    float(row["delta_hpwl_percent"])
                    for row in rows
                    if (row["case"], row["method"]) == key
                ]
                self.assertAlmostEqual(min(values), expected, delta=0.000001)
            self.assertTrue(any(row["pareto"] == "true" for row in rows))
            self.assertTrue((output / "figure5-runtime-quality.svg").is_file())

    def test_fresh_mode_consumes_actual_candidate_summary_shape(self):
        cases = [
            "aes_asap7", "aes_nangate45", "ariane133_nangate45", "ibex_asap7",
            "ibex_nangate45", "jpeg_asap7", "jpeg_nangate45",
            "swerv_wrapper_asap7", "swerv_wrapper_nangate45",
        ]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / "state"
            table = state / "paper_reproduction/table4/table4-fresh.tsv"
            table.parent.mkdir(parents=True)
            with table.open("w", newline="") as stream:
                writer = csv.DictWriter(
                    stream,
                    fieldnames=["case", "default_H_f", "default_runtime_s"],
                    delimiter="\t",
                )
                writer.writeheader()
                for case in cases:
                    writer.writerow({"case": case, "default_H_f": 1000, "default_runtime_s": 10})

            audit_path = (
                state / "experiment_batches/fresh_contract_paper9_place"
                / "candidate-eligibility-audit.json"
            )
            audit_path.parent.mkdir(parents=True)
            rounds = []
            for case in cases:
                eligible = []
                for iteration in range(1, 11):
                    for student_number in range(1, 5):
                        eligible.append(
                            {
                                "iteration": iteration,
                                "student": f"student_{student_number:02d}",
                                "run_tag": f"fresh_contract_{case}_iter_{iteration:02d}_student_{student_number:02d}",
                                "hpwl": 1000 - iteration - student_number / 10,
                                "runtime_seconds": 11 + student_number / 10,
                            }
                        )
                rounds.append({"case": case, "eligible": eligible, "rejected": []})
            audit_path.write_text(
                json.dumps({"schema_version": 1, "rounds": rounds}), encoding="utf-8"
            )

            for case in ("aes_nangate45", "ariane133_nangate45"):
                trials = (
                    state / "bo_runs"
                    / f"openroad_dpl_hpwl_only_9case_bo_paper9_place_{case}/trials.tsv"
                )
                trials.parent.mkdir(parents=True)
                with trials.open("w", newline="") as stream:
                    writer = csv.DictWriter(
                        stream,
                        fieldnames=["trial", "status", "runtime", "hpwl_final"],
                        delimiter="\t",
                    )
                    writer.writeheader()
                    for trial in range(400):
                        writer.writerow({
                            "trial": trial,
                            "status": "ok",
                            "runtime": 10 + trial / 1000,
                            "hpwl_final": 999 - trial / 100,
                        })

            for figure in ("figure4", "figure5"):
                output = root / figure
                result = subprocess.run(
                    [
                        sys.executable, str(SCRIPT), figure, "--source", "fresh",
                        "--artifact-root", str(ROOT), "--state-root", str(state),
                        "--run-prefix", "fresh_contract", "--output-dir", str(output),
                    ],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    check=True,
                )
                self.assertIn("[PASS]", result.stdout)
            with (root / "figure4/figure4-best-so-far.tsv").open(newline="") as stream:
                figure4_rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(figure4_rows), 99)
            self.assertTrue(any(row["promotion_status"] == "promoted" for row in figure4_rows))
            self.assertTrue(all(row["point_status"] == "observed" for row in figure4_rows))


if __name__ == "__main__":
    unittest.main()
