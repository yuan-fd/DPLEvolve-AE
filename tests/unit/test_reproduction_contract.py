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

    def test_interrupted_bootstrap_branch_resumes_only_from_clean_anchor(self):
        helper = (
            ROOT
            / "src/dpl_evolve_agent/scripts/workspace/git_prepare_helpers.sh"
        )
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory) / "repo"
            repo.mkdir()
            env = {
                **os.environ,
                "GIT_AUTHOR_NAME": "AE Test",
                "GIT_AUTHOR_EMAIL": "ae-test@example.invalid",
                "GIT_COMMITTER_NAME": "AE Test",
                "GIT_COMMITTER_EMAIL": "ae-test@example.invalid",
            }
            subprocess.run(["git", "init", "-q", repo], check=True, env=env)
            (repo / "README").write_text("anchor\n")
            subprocess.run(["git", "-C", repo, "add", "README"], check=True, env=env)
            subprocess.run(
                ["git", "-C", repo, "commit", "-qm", "anchor"], check=True, env=env
            )
            anchor = subprocess.check_output(
                ["git", "-C", repo, "rev-parse", "HEAD"], text=True
            ).strip()
            branch = "dplevolve-ae-prepared"
            subprocess.run(
                ["git", "-C", repo, "checkout", "-qb", branch], check=True
            )

            probe = (
                f'source "{helper}"; '
                'dpl_prepare_branch_is_resumable "$1" "$2" "$3"'
            )
            clean = subprocess.run(
                ["bash", "-c", probe, "resume-test", repo, branch, anchor]
            )
            self.assertEqual(clean.returncode, 0)

            (repo / "README").write_text("local change\n")
            dirty = subprocess.run(
                ["bash", "-c", probe, "resume-test", repo, branch, anchor]
            )
            self.assertNotEqual(dirty.returncode, 0)

            subprocess.run(
                ["git", "-C", repo, "checkout", "--", "README"], check=True
            )
            wrong_commit = subprocess.run(
                ["bash", "-c", probe, "resume-test", repo, branch, f"{anchor}^{{}}x"]
            )
            self.assertNotEqual(wrong_commit.returncode, 0)

    def test_reproduction_runtime_resolves_openroad_from_prepared_checkout_head(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orfs = root / "OpenROAD-flow-scripts"
            openroad = orfs / "tools" / "OpenROAD"
            (orfs / "flow").mkdir(parents=True)
            openroad.mkdir(parents=True)
            env = {
                **os.environ,
                "GIT_AUTHOR_NAME": "AE Test",
                "GIT_AUTHOR_EMAIL": "ae-test@example.invalid",
                "GIT_COMMITTER_NAME": "AE Test",
                "GIT_COMMITTER_EMAIL": "ae-test@example.invalid",
            }
            subprocess.run(["git", "init", "-q", openroad], check=True, env=env)
            (openroad / "README").write_text("prepared tree\n")
            subprocess.run(
                ["git", "-C", openroad, "add", "README"], check=True, env=env
            )
            subprocess.run(
                ["git", "-C", openroad, "commit", "-qm", "prepared"],
                check=True,
                env=env,
            )
            anchor = subprocess.check_output(
                ["git", "-C", openroad, "rev-parse", "--short", "HEAD"],
                text=True,
            ).strip()
            state = root / "state"
            binary = state / "openroad_core" / anchor / "install/OpenROAD/bin/openroad"
            binary.parent.mkdir(parents=True)
            binary.write_text("#!/bin/sh\nexit 0\n")
            binary.chmod(0o755)
            yosys = state / "yosys/8449dd470/bin/yosys"
            yosys.parent.mkdir(parents=True)
            yosys.write_text("#!/bin/sh\nexit 0\n")
            yosys.chmod(0o755)

            # Simulate a shared state directory whose environment file was
            # generated from another clone.  Even executable stale paths must
            # not leak into the current clone's resolved environment.
            stale = root / "previous-clone-state"
            stale_openroad = stale / "openroad"
            stale_yosys = stale / "yosys"
            for stale_binary in (stale_openroad, stale_yosys):
                stale_binary.parent.mkdir(parents=True, exist_ok=True)
                stale_binary.write_text("#!/bin/sh\nexit 0\n")
                stale_binary.chmod(0o755)
            environment = state / "ae/environment.sh"
            environment.parent.mkdir(parents=True)
            environment.write_text(
                f'export OPENROAD_EXE="{stale_openroad}"\n'
                f'export YOSYS_EXE="{stale_yosys}"\n'
            )

            probe_env = {
                key: value
                for key, value in os.environ.items()
                if key not in {"OPENROAD_EXE", "YOSYS_EXE"}
            }
            probe_env.update(
                {
                    "AE_ROOT": str(ROOT),
                    "DPL_EVOLVE_AGENT_ROOT": str(ROOT / "src/dpl_evolve_agent"),
                    "ORFS_ROOT": str(orfs),
                    "DPL_EVOLVE_STATE_ROOT": str(state),
                    "DPL_EVOLVE_PYTHON": sys.executable,
                }
            )
            result = subprocess.run(
                [
                    "bash",
                    "-c",
                    "source scripts/reproduce/common.sh; "
                    "repro_require_runtime; "
                    "printf '%s\\n%s\\n' \"$OPENROAD_EXE\" \"$YOSYS_EXE\"",
                ],
                cwd=ROOT,
                env=probe_env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=True,
            )
            self.assertEqual(result.stdout.splitlines(), [str(binary), str(yosys)])

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
                [
                    "bash", "scripts/reproduce/run_dse.sh", "--profile", "paper",
                    "--run-prefix", "contract_test", "--dry-run",
                ],
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
            "--run-prefix contract_test",
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

    def test_public_makefile_exposes_fresh_reproduction_only(self):
        makefile = (ROOT / "Makefile").read_text()
        self.assertNotIn("audit-archive", makefile)
        self.assertNotIn("\nevidence:", makefile)
        self.assertIn("reproduce-paper-results: paper-data-check", makefile)
        for target in ("reproduce-table4", "reproduce-table5", "reproduce-table6"):
            self.assertIn(f"$(MAKE) {target}", makefile)
        self.assertIn("reproduce-paper-search", makefile)
        self.assertIn("../.venvs/dplevolve/bin/python", makefile)

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
                    "runtime_seconds": 10.0 * target["bo_runtime"],
                }))
                bo_path.write_text(json.dumps({
                    "status": "ok",
                    "legalize_exit_status": 0,
                    "metrics_path": str(bo_metrics_path),
                    "metrics": {
                        "hpwl_final": bo_final,
                        "runtime": 10.0 * target["bo_runtime"],
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
                            "runtime_seconds": 10.0 * target[f"{track}_runtime"],
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

    def test_default_baseline_sweep_resolves_reported_metrics_paths(self):
        launcher = (
            ROOT
            / "src/dpl_evolve_agent/experiments/launchers/run_openroad_dpl_9case_baselines.sh"
        ).read_text()
        self.assertIn('metrics_path="${ORFS_ROOT}/flow/${metrics_path#./}"', launcher)
        self.assertIn('get("average_displacement_micron")', launcher)
        self.assertIn('get("max_displacement_micron")', launcher)

    def test_selected_replay_distinguishes_pinned_and_rebuilt_inputs(self):
        selected_root = ROOT / "artifacts/01-table4-qor/selected-programs"
        manifest = json.loads((selected_root / "manifest.json").read_text())

        def run(case: str, hpwl: float) -> subprocess.CompletedProcess[str]:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                metrics = root / "metrics.json"
                metrics.write_text(json.dumps({
                    "status": "ok",
                    # Clean pinned OpenROAD checks return an empty Tcl string
                    # and do not create an otherwise empty report file.
                    "legality": {"placement_violations": ""},
                }))
                results = root / "results.tsv"
                fields = [
                    "status", "hpwl_global_micron", "hpwl_legalized_micron",
                    "hpwl_after_improve_micron", "hpwl_after_micron",
                    "runtime_seconds", "candidate_metrics",
                ]
                with results.open("w", newline="") as stream:
                    writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
                    writer.writeheader()
                    writer.writerow({
                        "status": "PASS",
                        "hpwl_global_micron": hpwl + 3,
                        "hpwl_legalized_micron": hpwl + 2,
                        "hpwl_after_improve_micron": hpwl + 1,
                        "hpwl_after_micron": hpwl,
                        "runtime_seconds": 10,
                        "candidate_metrics": metrics,
                    })
                return subprocess.run(
                    [
                        sys.executable, str(selected_root / "verify.py"),
                        "--root", str(selected_root), "--case", case,
                        "--objective", "hpwl", "--results", str(results),
                    ],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                )

        rebuilt = next(item for item in manifest["programs"] if item["case"] == "aes_asap7")
        rebuilt_hpwl = rebuilt["tracks"]["hpwl"]["expected_hpwl"] * 1.10
        rebuilt_result = run("aes_asap7", rebuilt_hpwl)
        self.assertEqual(rebuilt_result.returncode, 0, rebuilt_result.stdout)
        self.assertIn("reconstructed-input replay is complete and legal", rebuilt_result.stdout)
        self.assertIn("informational", rebuilt_result.stdout)

        pinned = next(
            item for item in manifest["programs"] if item["case"] == "aes_nangate45"
        )
        rebuilt_tolerance = manifest["replay_contract"][
            "rebuilt_hpwl_relative_tolerance_percent"
        ]
        within_tolerance_hpwl = pinned["tracks"]["hpwl"]["expected_hpwl"] * (
            1.0 + 0.9 * rebuilt_tolerance / 100.0
        )
        within_tolerance_result = run("aes_nangate45", within_tolerance_hpwl)
        self.assertEqual(
            within_tolerance_result.returncode, 0, within_tolerance_result.stdout
        )
        self.assertIn("input-checksum-pinned rebuilt replay", within_tolerance_result.stdout)
        self.assertIn("not bit-for-bit replay", within_tolerance_result.stdout)

        pinned_hpwl = pinned["tracks"]["hpwl"]["expected_hpwl"] * 1.01
        pinned_result = run("aes_nangate45", pinned_hpwl)
        self.assertNotEqual(pinned_result.returncode, 0)
        self.assertIn("input-checksum-pinned rebuilt replay HPWL drift", pinned_result.stdout)

    def test_selected_replay_accepts_a_regenerated_input_for_numerical_review(self):
        selected_root = ROOT / "artifacts/01-table4-qor/selected-programs"
        manifest = json.loads((selected_root / "manifest.json").read_text())
        item = next(row for row in manifest["programs"] if row["case"] == "aes_nangate45")
        with tempfile.TemporaryDirectory() as directory:
            orfs = Path(directory)
            input_dir = (
                orfs
                / "flow/results"
                / item["platform"]
                / item["design"]
                / manifest["flow_variant"]
            )
            input_dir.mkdir(parents=True)
            (input_dir / manifest["input_stage"]).write_bytes(b"fresh rebuilt ODB")
            (input_dir / manifest["constraint_stage"]).write_text("create_clock\n")
            result = subprocess.run(
                [
                    sys.executable,
                    str(selected_root / "verify.py"),
                    "--root",
                    str(selected_root),
                    "--orfs-root",
                    str(orfs),
                    "--case",
                    "aes_nangate45",
                    "--objective",
                    "hpwl",
                    "--require-inputs",
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn("judged by legality and numerical tolerance", result.stdout)

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
        def reference_rows(name):
            with (ROOT / f"artifacts/03-table6-cutrow/inputs/{name}").open() as stream:
                return {
                    (row["case"], row["pattern"]): row
                    for row in csv.DictReader(stream, delimiter="\t")
                }

        review_reference = reference_rows("reviewdse.tsv")
        fixed_reference = reference_rows("fixed_routes.tsv")
        case_labels = {
            "ariane133_placebatch": "Ariane133 N45",
            "swerv_wrapper_dense2": "SWERV dense N45",
            "bp_quad_placebatch": "BPQUAD",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            results = root / "results.tsv"
            fields = [
                "case", "pattern", "role", "status", "exit_code", "runtime_seconds",
                "hpwl_before_micron", "hpwl_after_micron", "delta_percent",
                "avg_disp_um", "max_disp_um", "check_result", "timeout_seconds",
                "metrics_json", "log",
            ]
            with results.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                for spec in contract["table6"]["rows"]:
                    paper_key = (case_labels[spec["case"]], spec["paper_pattern"])
                    for role in ("diamond", "negotiation", "reviewdse"):
                        status = spec["expected"][role]
                        hpwl_before = hpwl_after = None
                        if role == "reviewdse":
                            ref = review_reference[paper_key]
                            runtime = float(ref["runtime_seconds"])
                            hpwl_before = float(ref["hpwl_before"])
                            hpwl_after = float(ref["hpwl_after"])
                        elif status == "pass":
                            ref = fixed_reference[paper_key]
                            runtime = float(ref[f"{role}_runtime_seconds"])
                            hpwl_after = float(ref[f"{role}_hpwl_after"])
                            hpwl_before = float(review_reference[paper_key]["hpwl_before"])
                        elif status == "timeout":
                            runtime = 7200.0
                        else:
                            runtime = 1.0
                        metrics = root / f"{spec['case']}-{spec['pattern']}-{role}.json"
                        metrics.write_text(
                            json.dumps(
                                {
                                    "status": "ok" if status == "pass" else (
                                        "timeout" if status == "timeout" else "error"
                                    ),
                                    "runtime_seconds": runtime,
                                    "hpwl": {
                                        "source": "openroad_dpl_log",
                                        "before_micron": hpwl_before,
                                        "after_micron": hpwl_after,
                                    },
                                    "displacement": {
                                        "average_displacement_micron": 1.0,
                                        "max_displacement_micron": 2.0,
                                    },
                                    "legality": {
                                        "check_status": 0 if status == "pass" else (
                                            "not_run" if status == "timeout" else 1
                                        )
                                    },
                                }
                            )
                        )
                        writer.writerow({
                            "case": spec["case"],
                            "pattern": spec["pattern"],
                            "role": role,
                            "status": status,
                            "exit_code": 0 if status == "pass" else (124 if status == "timeout" else 1),
                            "runtime_seconds": runtime,
                            "hpwl_before_micron": hpwl_before,
                            "hpwl_after_micron": hpwl_after,
                            "avg_disp_um": 1.0,
                            "max_disp_um": 2.0,
                            "check_result": "clean" if status == "pass" else status,
                            "timeout_seconds": 7200,
                            "metrics_json": metrics,
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
            review_rows = [row for row in rows if row["role"] == "reviewdse"]
            self.assertTrue(all(row["metrics_contract"] == "pass" for row in review_rows))
            comparable = [row for row in review_rows if row["legal_fixed_role"]]
            self.assertEqual(len(comparable), 2)
            self.assertTrue(all(float(row["qor_improvement_percent"]) > 0 for row in comparable))


if __name__ == "__main__":
    unittest.main()
