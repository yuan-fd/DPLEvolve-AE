import json
import csv
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ReproductionContractTests(unittest.TestCase):
    def run_python(self, script: str, *args: object) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(ROOT / "scripts" / "reproduce" / script), *map(str, args)],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=True,
        )

    def test_paper_protocol_matches_manuscript(self):
        manifest = json.loads(
            (ROOT / "configs/reproduction/paper-experiments.json").read_text()
        )
        table4 = manifest["table4"]
        self.assertEqual(len(table4["cases"]), 9)
        self.assertEqual(table4["bo"]["trials_per_case"], 400)
        self.assertEqual(table4["bo"]["parallel_trials_per_case"], 4)
        self.assertEqual(table4["reviewdse"]["teacher"]["model"], "gpt-5.5")
        self.assertEqual(table4["reviewdse"]["students"]["count"], 4)
        self.assertEqual(table4["reviewdse"]["students"]["model"], "gpt-5.4")
        self.assertEqual(table4["reviewdse"]["iterations"], 10)
        self.assertEqual(manifest["paper"]["runtime_gate"], 2.0)

    def test_paper_dse_plan_is_non_mutating_and_exact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / "state-that-must-not-be-created"
            result = subprocess.run(
                ["bash", "scripts/reproduce/run_dse.sh", "--profile", "paper", "--dry-run"],
                cwd=ROOT,
                env={
                    **os.environ,
                    "DPL_EVOLVE_STATE_ROOT": str(state),
                    "ORFS_ROOT": str(root / "missing-orfs-is-allowed-for-plan"),
                    "DPL_EVOLVE_PYTHON": sys.executable,
                },
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=True,
            )
            self.assertFalse(state.exists())
        output = result.stdout
        for fragment in (
            "--teacher-model gpt-5.5",
            "--student-model gpt-5.4",
            "--children 4",
            "--iterations 10",
            "--runtime-multiplier 2.0",
            "--level1-evidence",
        ):
            self.assertIn(fragment, output)

    def test_table5_and_table6_refuse_missing_exact_data(self):
        missing_root = ROOT / "tests" / "definitely-missing-paper-data"
        for script in ("reproduce_table5.sh", "reproduce_table6.sh"):
            result = subprocess.run(
                ["bash", f"scripts/reproduce/{script}", "--check-inputs"],
                cwd=ROOT,
                env={"PATH": "/usr/bin:/bin", "PAPER_DATA_ROOT": str(missing_root)},
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 3)
            self.assertIn("[BLOCKED]", result.stdout)

    def test_table6_contract_uses_retained_def_data_and_one_source(self):
        manifest = json.loads(
            (ROOT / "configs/reproduction/paper-experiments.json").read_text()
        )
        table6 = manifest["table6"]
        self.assertIn("cutrows.def", table6["input_format"])
        self.assertIn("no paper-time ODB", table6["input_format"])
        self.assertEqual(table6["reviewdse_program"], "evolved_negotiation")
        self.assertEqual(
            {row["program"] for row in table6["rows"]},
            {"evolved_negotiation"},
        )
        runner = (ROOT / "scripts/reproduce/reproduce_table6.sh").read_text()
        self.assertIn("cutrows.def.gz", runner)
        self.assertIn("cutrows.v.gz", runner)
        self.assertNotIn("3_4_place_resized.odb", runner)
        self.assertIn("--pattern requires --case", runner)
        fetcher = (ROOT / "scripts/reproduce/fetch_table6_data.sh").read_text()
        self.assertIn("releases/download/paper-data-v1", fetcher)
        self.assertIn("gh release download", fetcher)
        self.assertIn(
            "c73f84c6008ddf578bce9c2708dbe1eff55b2a8e96dada95376369afe9008b63",
            fetcher,
        )

    def test_table5_uses_retained_recipes_and_refuses_a_fake_swerv_substitute(self):
        prepare = (ROOT / "scripts/reproduce/prepare_table5_inputs.sh").read_text()
        for fragment in (
            "aes_dense_nangate45 DENSE default",
            "jpeg_util90_nangate45 DENSE 90",
            "config_dense2.mk",
            "DESIGN_CONFIG=\"${SWERV_DESIGN_CONFIG}\" FLOW_VARIANT=DENSE_2",
        ):
            self.assertIn(fragment, prepare)
        self.assertNotIn("swerv_wrapper_nangate45 DENSE_2 60", prepare)
        runner = (ROOT / "scripts/reproduce/reproduce_table5.sh").read_text()
        self.assertIn("prepare_table5_inputs.sh", runner)
        self.assertNotIn("PAPER_DATA_ROOT}/table5/${row_id}/input/3_4", runner)

    def test_external_paper_data_must_be_fully_checksummed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = root / "table5/case/input/2_floorplan.sdc"
            payload.parent.mkdir(parents=True)
            payload.write_text("create_clock\n")
            digest = hashlib.sha256(payload.read_bytes()).hexdigest()
            manifest = root / "table5/MANIFEST.sha256"
            manifest.write_text(f"{digest}  table5/case/input/2_floorplan.sdc\n")
            self.run_python("verify_data_manifest.py", "--root", root, "--scope", "table5")

            (root / "table5/case/input/unsigned.odb").write_bytes(b"unsigned")
            result = subprocess.run(
                [sys.executable, "scripts/reproduce/verify_data_manifest.py", "--root", root, "--scope", "table5"],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsigned files", result.stdout)

    def test_archive_alias_is_labeled_non_reproduction(self):
        makefile = (ROOT / "Makefile").read_text()
        self.assertIn("does not reproduce the paper experiments", makefile)
        self.assertIn("audit-archive", makefile)
        self.assertIn("reproduce-paper-results: paper-data-check", makefile)
        for target in ("reproduce-table4", "reproduce-table5", "reproduce-table6"):
            self.assertIn(f"$(MAKE) {target}", makefile)
        self.assertIn("reproduce-paper-search", makefile)

    def test_bo_space_matches_manifest_and_archived_trial_columns(self):
        manifest = json.loads(
            (ROOT / "configs/reproduction/paper-experiments.json").read_text()
        )
        space = (
            ROOT
            / "src/dpl_evolve_agent/configs/bo_search_spaces"
            / "openroad_dpl_native_dpo_frontier.yaml"
        ).read_text()
        yaml_parameters = re.findall(r"^\s*- name:\s*([A-Za-z0-9_]+)\s*$", space, re.MULTILINE)
        archived_header = (
            ROOT / "artifacts/01-table4-qor/inputs/bo_paper/aes_asap7.trials.tsv"
        ).read_text().splitlines()[0].split("\t")
        expected = manifest["table4"]["bo"]["parameters"]
        self.assertEqual(yaml_parameters, expected)
        self.assertEqual(archived_header[-len(expected):], expected)
        launcher = (
            ROOT / "src/dpl_evolve_agent/experiments/launchers/run_bo_9case_openroad_dpl.sh"
        ).read_text()
        self.assertIn("openroad_dpl_native_dpo_frontier.yaml", launcher)
        self.assertIn("openroad_dpl_hpwl_only_9case_bo", launcher)

    def test_table4_summary_consumes_fresh_metrics(self):
        selected_path = ROOT / "artifacts/01-table4-qor/selected-programs/manifest.json"
        expected_path = ROOT / "artifacts/01-table4-qor/expected/table4.json"
        selected = json.loads(selected_path.read_text())
        expected = json.loads(expected_path.read_text())["cases"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orfs = root / "orfs"
            state = root / "state"
            for spec in selected["programs"]:
                case = spec["case"]
                target = expected[case]
                default_path = (
                    orfs / "flow/reports" / spec["platform"] / spec["design"]
                    / "paper9_place/dpl_evolve_baseline"
                    / f"bo9_openroad_dpl_flow_{case}/metrics.json"
                )
                default_path.parent.mkdir(parents=True, exist_ok=True)
                default_path.write_text(json.dumps({
                    "status": "ok",
                    "legality": {"placement_violations": 0},
                    "hpwl": {"after_micron": 1000.0},
                    "hpwl_stages": {
                        "global_micron": 1100.0,
                        "legalized_micron": 1020.0,
                        "after_improve_micron": 1005.0,
                        "final_micron": 1000.0,
                    },
                    "runtime_seconds": 10.0,
                }))

                bo_path = (
                    state / "bo_runs"
                    / f"openroad_dpl_hpwl_only_9case_bo_paper9_place_{case}/best.json"
                )
                bo_path.parent.mkdir(parents=True, exist_ok=True)
                bo_final = 1000.0 * (1.0 + target["bo_delta"] / 100.0)
                bo_metrics_path = bo_path.parent / "winner-metrics.json"
                bo_metrics_path.write_text(json.dumps({
                    "status": "ok",
                    "legality": {"placement_violations": 0},
                    "hpwl": {"after_micron": bo_final},
                    "hpwl_stages": {
                        "global_micron": 1100.0,
                        "legalized_micron": 1020.0,
                        "after_improve_micron": bo_final + 1.0,
                        "final_micron": bo_final,
                    },
                    "runtime_seconds": 11.0,
                }))
                bo_path.write_text(json.dumps({
                    "status": "ok",
                    "legalize_exit_status": 0,
                    "metrics_path": str(bo_metrics_path),
                    "metrics": {
                        "hpwl_final": bo_final,
                        "runtime": 11.0,
                    },
                }))

                for track, key in (("hpwl", "hpwl_delta"), ("ghr", "ghr_delta")):
                    run_id = f"paper_table4_{track}_{case}"
                    replay = state / "paper_reproduction/table4" / run_id / run_id / "results.tsv"
                    replay.parent.mkdir(parents=True, exist_ok=True)
                    replay_metrics = replay.parent / "metrics.json"
                    replay_metrics.write_text(json.dumps({
                        "status": "ok",
                        "legality": {"placement_violations": 0},
                        "hpwl_stages": {
                            "global_micron": 1100.0,
                            "legalized_micron": 1020.0,
                            "after_improve_micron": 1005.0,
                            "final_micron": 1000.0 * (1.0 + target[key] / 100.0),
                        },
                    }))
                    with replay.open("w", newline="") as stream:
                        writer = csv.DictWriter(
                            stream,
                            fieldnames=[
                                "status", "hpwl_global_micron", "hpwl_legalized_micron",
                                "hpwl_after_improve_micron", "hpwl_after_micron",
                                "runtime_seconds", "candidate_metrics",
                            ],
                            delimiter="\t",
                        )
                        writer.writeheader()
                        writer.writerow({
                            "status": "PASS",
                            "hpwl_global_micron": 1100.0,
                            "hpwl_legalized_micron": 1020.0,
                            "hpwl_after_improve_micron": 1005.0,
                            "hpwl_after_micron": 1000.0 * (1.0 + target[key] / 100.0),
                            "runtime_seconds": 12.0,
                            "candidate_metrics": replay_metrics,
                        })

            output = root / "table4-fresh.tsv"
            self.run_python(
                "summarize_table4.py",
                "--orfs-root", orfs,
                "--state-root", state,
                "--flow-variant", "paper9_place",
                "--selected-manifest", selected_path,
                "--expected", expected_path,
                "--output", output,
            )
            with output.open(newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(rows), 10)
            self.assertEqual(rows[-1]["case"], "Mean")

    def test_table5_summary_derives_counterexamples_from_fresh_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fresh = root / "fresh.tsv"
            fields = ["row_id", "role", "case", "status", "H_g", "H_lg", "H_ip", "H_f"]
            with fresh.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                for row_id in ("aes_dense_n45", "jpeg_dense_n45", "swerv_dense_n45"):
                    writer.writerow({"row_id": row_id, "role": "selected", "case": row_id, "status": "PASS", "H_g": 120, "H_lg": 90, "H_ip": 105, "H_f": 110})
                    writer.writerow({"row_id": row_id, "role": "reference", "case": row_id, "status": "PASS", "H_g": 120, "H_lg": 100, "H_ip": 99, "H_f": 100})
            output = root / "table5-fresh.tsv"
            self.run_python("summarize_table5.py", "--fresh-runs", fresh, "--output", output)
            with output.open(newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual([row["verdict"] for row in rows], ["counterexample"] * 3)

    def test_table6_summary_classifies_fresh_status_and_legality(self):
        contract_path = ROOT / "configs/reproduction/paper-experiments.json"
        contract = json.loads(contract_path.read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            results = root / "results.tsv"
            fields = ["case", "pattern", "role", "status", "exit_code", "runtime_seconds", "metrics_json", "log"]
            with results.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                for spec in contract["table6"]["rows"]:
                    for role in ("diamond", "negotiation", "reviewdse"):
                        writer.writerow({
                            "case": spec["case"],
                            "pattern": spec["pattern"],
                            "role": role,
                            "status": spec["expected"][role],
                            "exit_code": 0 if spec["expected"][role] == "pass" else 1,
                            "runtime_seconds": 1,
                            "metrics_json": root / f"{spec['case']}-{spec['pattern']}-{role}.json",
                            "log": root / "run.log",
                        })
            output = root / "table6-fresh.tsv"
            self.run_python(
                "summarize_table6.py",
                "--results", results,
                "--experiment-manifest", contract_path,
                "--output", output,
            )
            with output.open(newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual(len(rows), 27)
            self.assertTrue(all(row["verdict"] == "match" for row in rows))


if __name__ == "__main__":
    unittest.main()
