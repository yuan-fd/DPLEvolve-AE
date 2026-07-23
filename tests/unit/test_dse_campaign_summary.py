import json
import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "reproduce"))

from summarize_dse_campaign import (
    aggregate_round_usage,
    gain_hr,
    select_winners,
    summarize_round,
)
from scripts.evaluator.candidate_provenance import file_sha256, source_ref_fingerprint


class DseCampaignSummaryTests(unittest.TestCase):
    def test_hpwl_and_ghr_tracks_are_selected_from_same_population(self):
        candidates = [
            {
                "student": "student_01", "iteration": 3,
                "hpwl": 90.0, "runtime_seconds": 20.0,
                "gain_hr": gain_hr(100.0, 90.0, 2.0),
            },
            {
                "student": "student_02", "iteration": 7,
                "hpwl": 90.5, "runtime_seconds": 10.0,
                "gain_hr": gain_hr(100.0, 90.5, 1.0),
            },
        ]
        hpwl, ghr = select_winners(candidates)
        self.assertEqual(hpwl["student"], "student_01")
        self.assertEqual(ghr["student"], "student_02")
        self.assertAlmostEqual(gain_hr(100.0, 90.0, 2.0), 9.0)

    def test_token_accounting_deduplicates_cumulative_thread_snapshots(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            operations = root / "checkpoints" / "operations"
            snapshots = [
                ("student_iter1", "student-thread", 100, 80, 10),
                ("student_iter2", "student-thread", 150, 120, 20),
                ("teacher", "teacher-thread", 50, 0, 10),
            ]
            for operation, thread, input_tokens, cached, output in snapshots:
                path = operations / operation / "codex_usage_summary.json"
                path.parent.mkdir(parents=True)
                path.write_text(
                    json.dumps(
                        {
                            "returncode": 0,
                            "thread_id": thread,
                            "usage": {
                                "input_tokens": input_tokens,
                                "cached_input_tokens": cached,
                                "output_tokens": output,
                            },
                        }
                    ),
                    encoding="utf-8",
                )
            usage = aggregate_round_usage(root)
            self.assertEqual(usage["operations"], 3)
            self.assertEqual(usage["sessions"], 2)
            self.assertEqual(usage["logged_tokens"], 230)
            self.assertEqual(usage["active_tokens"], 110)

    def test_paper_manifest_records_baseline_and_level1_fingerprints(self):
        text = (
            ROOT / "src/dpl_evolve_agent/scripts/teacher_loop/orchestrator.py"
        ).read_text(encoding="utf-8")
        self.assertIn('"baseline_metrics"', text)
        self.assertIn('"level1_evidence_sha256"', text)

    def test_one_candidate_campaign_is_summarized_through_protected_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orfs = root / "orfs"
            round_id = "paper_round"
            case = "gcd_nangate45"
            run_tag = f"{round_id}_iter_01_student_01_route_{case}"
            round_root = root / "state" / round_id
            round_dir = round_root / "teacher_rounds"
            student = round_dir / "students/student_01"
            artifact = student / "iter_01/artifacts"
            source = student / "workspace/variant/dpl_evolve"
            source.mkdir(parents=True)
            artifact.mkdir(parents=True)
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            subprocess.run(["git", "-C", str(source), "config", "user.email", "ae@test"], check=True)
            subprocess.run(["git", "-C", str(source), "config", "user.name", "AE"], check=True)
            (source / "candidate.cpp").write_text("int candidate = 1;\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(source), "add", "candidate.cpp"], check=True)
            subprocess.run(["git", "-C", str(source), "commit", "-qm", "candidate"], check=True)
            commit = subprocess.check_output(
                ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
            ).strip()
            ref = f"candidate/{run_tag}"
            subprocess.run(["git", "-C", str(source), "branch", ref, commit], check=True)
            (student / "lineage.json").write_text(
                json.dumps({"iterations": [{"iteration": "iter_01", "source_candidate_ref": ref}]}),
                encoding="utf-8",
            )
            (artifact / "source_commit.json").write_text(
                json.dumps({"source_commit": commit, "source_candidate_ref": ref}), encoding="utf-8"
            )
            (artifact / "implementation.diff").write_text("diff --git a/a b/a\n", encoding="utf-8")
            (artifact / "knowledge_card.md").write_text("liveness observed\n", encoding="utf-8")
            binary = student / "workspace/variant/install/OpenROAD/bin/openroad"
            binary.parent.mkdir(parents=True)
            binary.write_text("binary\n", encoding="utf-8")

            metrics = (
                orfs / "flow/reports/nangate45/gcd/paper9_place/dpl_evolve_baseline"
                / run_tag / "metrics.json"
            )
            metrics.parent.mkdir(parents=True)
            metrics.write_text(
                json.dumps(
                    {
                        "status": "ok", "legalize_exit_status": 0,
                        "manifest": {"line": "evolve_default", "run_tag": run_tag},
                        "runtime_seconds": 15.0,
                        "hpwl": {
                            "source": "openroad_dpl_pin_hpwl", "before_micron": 110.0,
                            "after_micron": 90.0, "delta_micron": -20.0,
                        },
                        "hpwl_stages": {
                            "global_micron": 110.0, "legalized_micron": 100.0,
                            "after_improve_micron": 95.0, "final_micron": 90.0,
                        },
                        "displacement": {
                            "average_displacement_micron": 1.0,
                            "max_displacement_micron": 3.0,
                        },
                        "legality": {"placement_violations": "0"},
                        "legalization": {"legalizer_mode": "evolve_default"},
                    }
                ),
                encoding="utf-8",
            )
            (artifact / "candidate_metrics_summary.json").write_text(
                json.dumps({"eligibility": {"eligible": True, "problems": []}}), encoding="utf-8"
            )
            (artifact / "candidate_build_provenance.json").write_text(
                json.dumps({"status": "complete"}), encoding="utf-8"
            )
            (artifact / "candidate_evaluation_provenance.json").write_text(
                json.dumps(
                    {
                        "status": "verified",
                        "source_fingerprint": source_ref_fingerprint(source, ref),
                        "metrics_sha256": file_sha256(metrics),
                        "metrics_run_tag": run_tag,
                    }
                ),
                encoding="utf-8",
            )
            operation = round_root / "checkpoints/operations" / run_tag.removesuffix(f"_{case}")
            operation.mkdir(parents=True)
            (operation / "codex_usage_summary.json").write_text(
                json.dumps(
                    {
                        "returncode": 0, "agent_message_count": 1, "thread_id": "student",
                        "usage": {"input_tokens": 100, "cached_input_tokens": 80, "output_tokens": 10},
                    }
                ),
                encoding="utf-8",
            )
            (operation / "codex_last_message.txt").write_text("done\n", encoding="utf-8")
            manifest = {
                "round_id": round_id, "case": case, "flow_variant": "paper9_place",
                "start_kind": "framework", "teacher_model": "gpt-5.5",
                "teacher_reasoning_effort": "xhigh", "student_model": "gpt-5.4",
                "student_reasoning_effort": "xhigh", "student_runtime_multiplier": 2.0,
                "level1_evidence_source": "/frozen/level1.md", "level1_evidence_sha256": "abc",
                "baseline_metrics": {
                    "openroad_dpl_flow": {
                        "hpwl_after": 100.0, "runtime_seconds": 10.0,
                        "metrics_path": "/baseline/metrics.json",
                    }
                },
                "iterations": [
                    {
                        "iteration": "iter_01",
                        "children": [{
                            "student_id": "student_01", "route_label": "route", "run_tag": run_tag,
                        }],
                    }
                ],
            }
            (round_dir / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            row, audit = summarize_round(
                round_root=round_root, orfs_root=orfs,
                expected_iterations=1, expected_children=1,
            )
            self.assertEqual(row["eligible_candidates"], 1)
            self.assertEqual(row["hpwl_iteration"], 1)
            self.assertEqual(row["ghr_student"], "student_01")
            self.assertEqual(row["tokens_logged_tokens"], 110)
            self.assertEqual(audit["rejected"], [])


if __name__ == "__main__":
    unittest.main()
