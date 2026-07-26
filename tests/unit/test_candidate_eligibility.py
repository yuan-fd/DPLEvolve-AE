import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[2]
AGENT_ROOT = ROOT / "src" / "dpl_evolve_agent"
sys.path.insert(0, str(AGENT_ROOT))

from scripts.evaluator.candidate_eligibility import metric_eligibility
from scripts.evaluator.candidate_provenance import (
    file_sha256,
    source_ref_fingerprint,
    source_worktree_fingerprint,
)
from scripts.evaluator.evaluation_trial import allocate_trial
from scripts.teacher_loop.common import MetricSummary
from scripts.teacher_loop.common import student_workspace_paths
from scripts.teacher_loop.evidence import candidate_artifact_problems
from scripts.teacher_loop.workspace_scripts import write_student_workspace_scripts


def valid_metric_summary() -> dict:
    return {
        "status": "ok",
        "legalize_exit_status": 0,
        "canonical": {
            "hpwl_source": "openroad_dpl_pin_hpwl",
            "final_hpwl_micron": 90.0,
            "runtime_seconds": 19.0,
            "avg_displacement_micron": 1.0,
            "max_displacement_micron": 3.0,
            "legality": "clean",
        },
        "stages": {
            "global_micron": 110.0,
            "legalized_micron": 100.0,
            "after_improve_micron": 95.0,
            "final_micron": 90.0,
        },
        "headline_vs_openroad_default": {"runtime_ratio": 1.9},
        "log_counter_lines": ["[INFO DPL-9999] guided pass summary: accepted=1"],
    }


class CandidateEligibilityTests(unittest.TestCase):
    def test_evaluation_trial_allocator_never_reuses_or_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "evaluation_trials"
            first = allocate_trial(root, timestamp="20260726_120000")
            marker = first / "metrics.json"
            marker.write_text("first immutable result\n", encoding="utf-8")
            before = file_sha256(marker)

            second = allocate_trial(root, timestamp="20260726_120000")

            third = allocate_trial(root, timestamp="20260726_120100")

            self.assertEqual(first.name, "eval_001_20260726_120000")
            self.assertEqual(second.name, "eval_002_20260726_120000")
            self.assertEqual(third.name, "eval_003_20260726_120100")
            self.assertNotEqual(first, second)
            self.assertEqual(file_sha256(marker), before)
            self.assertFalse((second / "metrics.json").exists())

    def test_complete_candidate_passes_exact_metric_contract(self):
        verdict = metric_eligibility(valid_metric_summary())
        self.assertTrue(verdict["eligible"])
        self.assertEqual(verdict["problems"], [])
        self.assertEqual(verdict["runtime_gate"]["policy"], "exact_ratio_lte_limit")

    def test_each_scientific_gate_can_reject(self):
        cases = {
            "runtime_gate_exceeded": ("headline_vs_openroad_default", "runtime_ratio", 2.000001),
            "missing_stage_legalized_micron": ("stages", "legalized_micron", None),
            "missing_average_displacement": ("canonical", "avg_displacement_micron", None),
            "placement_not_clean": ("canonical", "legality", "overlap=1"),
            "noncanonical_hpwl": ("canonical", "hpwl_source", "cell_bbox_proxy"),
        }
        for expected, (section, key, value) in cases.items():
            with self.subTest(expected=expected):
                summary = valid_metric_summary()
                summary[section][key] = value
                self.assertIn(expected, metric_eligibility(summary)["problems"])
        summary = valid_metric_summary()
        summary["log_counter_lines"] = []
        self.assertIn("mechanism_liveness_unproven", metric_eligibility(summary)["problems"])

    def test_source_worktree_and_committed_ref_have_same_fingerprint(self):
        with tempfile.TemporaryDirectory() as directory:
            repo = Path(directory)
            subprocess.run(["git", "init", "-q", str(repo)], check=True)
            subprocess.run(["git", "-C", str(repo), "config", "user.email", "ae@test"], check=True)
            subprocess.run(["git", "-C", str(repo), "config", "user.name", "AE"], check=True)
            (repo / "algorithm.cpp").write_text("int candidate = 1;\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(repo), "add", "algorithm.cpp"], check=True)
            subprocess.run(["git", "-C", str(repo), "commit", "-qm", "candidate"], check=True)
            self.assertEqual(
                source_worktree_fingerprint(repo), source_ref_fingerprint(repo, "HEAD")
            )

    def test_promotion_gate_requires_and_checks_full_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            round_id = "round"
            case_id = "gcd_nangate45"
            tag = f"{round_id}_iter_00_student_01_route_{case_id}"
            round_dir = root / round_id / "teacher_rounds"
            student = round_dir / "students" / "student_01"
            source = student / "workspace" / "variant" / "dpl_evolve"
            source.mkdir(parents=True)
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            subprocess.run(["git", "-C", str(source), "config", "user.email", "ae@test"], check=True)
            subprocess.run(["git", "-C", str(source), "config", "user.name", "AE"], check=True)
            (source / "algorithm.cpp").write_text("int candidate = 1;\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(source), "add", "algorithm.cpp"], check=True)
            subprocess.run(["git", "-C", str(source), "commit", "-qm", "candidate"], check=True)
            commit = subprocess.check_output(
                ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
            ).strip()
            candidate_ref = f"candidate/{tag}"
            subprocess.run(["git", "-C", str(source), "branch", candidate_ref, commit], check=True)

            artifact = student / "iter_00" / "artifacts"
            artifact.mkdir(parents=True)
            (artifact / "implementation.diff").write_text("diff --git a/a b/a\n", encoding="utf-8")
            (artifact / "knowledge_card.md").write_text("observed counter activity\n", encoding="utf-8")
            (artifact / "source_commit.json").write_text(
                json.dumps({"source_commit": commit, "source_candidate_ref": candidate_ref}),
                encoding="utf-8",
            )
            (student / "lineage.json").write_text(
                json.dumps({"iterations": [{"iteration": "iter_00", "source_candidate_ref": candidate_ref}]}),
                encoding="utf-8",
            )
            binary = student / "workspace" / "variant" / "install/OpenROAD/bin/openroad"
            binary.parent.mkdir(parents=True)
            binary.write_text("binary\n", encoding="utf-8")

            metrics = root / "reports" / tag / "metrics.json"
            metrics.parent.mkdir(parents=True)
            metrics.write_text('{"fresh": true}\n', encoding="utf-8")
            metric_verdict = metric_eligibility(valid_metric_summary())
            (artifact / "candidate_metrics_summary.json").write_text(
                json.dumps({"eligibility": metric_verdict}), encoding="utf-8"
            )
            (artifact / "candidate_build_provenance.json").write_text(
                json.dumps({"status": "complete"}), encoding="utf-8"
            )
            (artifact / "candidate_evaluation_provenance.json").write_text(
                json.dumps(
                    {
                        "status": "verified",
                        "source_fingerprint": source_ref_fingerprint(source, candidate_ref),
                        "metrics_sha256": file_sha256(metrics),
                        "metrics_run_tag": tag,
                    }
                ),
                encoding="utf-8",
            )
            operation = root / "operations" / tag.removesuffix(f"_{case_id}")
            operation.mkdir(parents=True)
            (operation / "codex_usage_summary.json").write_text(
                json.dumps({"returncode": 0, "agent_message_count": 1}), encoding="utf-8"
            )
            (operation / "codex_last_message.txt").write_text("done\n", encoding="utf-8")
            candidate = MetricSummary(
                tag=tag,
                metrics_path=metrics,
                mode="evolve_default",
                line="evolve_default",
                run_tag=tag,
                hpwl_after=90.0,
                hpwl_delta=-10.0,
                runtime_seconds=19.0,
                avg_disp=1.0,
                max_disp=3.0,
                violations="",
            )
            runtime = SimpleNamespace(operations_dir=root / "operations")
            kwargs = dict(
                runtime=runtime,
                round_dir=round_dir,
                round_id=round_id,
                case_id=case_id,
                candidate=candidate,
            )
            self.assertEqual(candidate_artifact_problems(**kwargs), [])
            metrics.write_text('{"fresh": false}\n', encoding="utf-8")
            self.assertIn("metrics_provenance_mismatch", candidate_artifact_problems(**kwargs))

    def test_generated_student_helpers_are_shell_syntax_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = student_workspace_paths(
                round_dir=root / "round" / "teacher_rounds",
                iteration=1,
                student_id="student_01",
                stable_workspace=True,
            )
            runtime = SimpleNamespace(
                agent_root=AGENT_ROOT,
                orfs_root=root / "OpenROAD-flow-scripts",
                state_root=root / "state",
            )
            scripts = write_student_workspace_scripts(
                runtime=runtime,
                paths=paths,
                case_id="gcd_nangate45",
                flow_variant="paper",
                threads=4,
                start_kind="agenticflow_start",
                run_tag="round_iter_01_student_01_route_gcd_nangate45",
                parent_src=root / "parent",
                use_seed_override=False,
                timeout_seconds=20,
            )
            for name, path in scripts.items():
                if path.suffix == ".sh":
                    result = subprocess.run(
                        ["bash", "-n", str(path)], text=True,
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    )
                    self.assertEqual(result.returncode, 0, f"{name}: {result.stdout}")
            evaluate = scripts["evaluate"].read_text(encoding="utf-8")
            self.assertIn("evaluation_trials", evaluate)
            self.assertIn("TRIAL_RUN_TAG", evaluate)
            self.assertIn("trial_output_odb", evaluate)

    def test_protected_run_contract_detects_mutation_and_uses_hash_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            state = root / "state"
            manifest = root / "run_manifest.json"
            input_odb = root / "input.odb"
            evaluator = root / "evaluator.tcl"
            binary = root / "openroad"
            manifest.write_text("{}\n", encoding="utf-8")
            input_odb.write_bytes(b"large immutable input")
            evaluator.write_text("puts ok\n", encoding="utf-8")
            binary.write_bytes(b"binary")
            script = AGENT_ROOT / "scripts/evaluator/run_provenance.py"
            env = {**os.environ, "DPL_EVOLVE_STATE_ROOT": str(state)}
            subprocess.run(
                [
                    sys.executable, str(script), "--manifest", str(manifest),
                    "--input-snapshot", str(input_odb),
                    "--openroad-binary", str(binary),
                    "--protected-file", str(evaluator),
                ],
                check=True, env=env,
            )
            subprocess.run(
                [sys.executable, str(script), "--manifest", str(manifest), "--verify"],
                check=True, env=env,
            )
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            self.assertTrue(payload["protected_evaluation"]["unchanged"])
            self.assertTrue((state / "provenance/protected_file_hashes.json").is_file())
            evaluator.write_text("puts changed\n", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(script), "--manifest", str(manifest), "--verify"],
                env=env,
            )
            self.assertEqual(result.returncode, 3)
            payload = json.loads(manifest.read_text(encoding="utf-8"))
            self.assertFalse(payload["protected_evaluation"]["unchanged"])


if __name__ == "__main__":
    unittest.main()
