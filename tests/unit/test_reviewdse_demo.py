import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "scripts" / "demo" / "reviewdse_demo_dashboard.py"
SPEC = importlib.util.spec_from_file_location("reviewdse_demo_dashboard", MODULE_PATH)
assert SPEC and SPEC.loader
dashboard = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = dashboard
SPEC.loader.exec_module(dashboard)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload) + "\n", encoding="utf-8")


class ReviewDSEDemoTests(unittest.TestCase):
    def make_state(self, root: Path) -> tuple[Path, Path, str]:
        state = root / "state"
        batch = state / "experiment_batches" / "demo_paper9_place"
        batch.mkdir(parents=True)
        round_id = "demo_paper9_place_aes_nangate45_4x2_tgpt56sol_xhigh_sgpt56terra_high"
        (batch / "experiments.tsv").write_text(
            "case\tflow_variant\tround_id\tstart_kind\tchildren\titerations\tteacher\tstudent\n"
            f"aes_nangate45\tpaper9_place\t{round_id}\tframework\t4\t2\t"
            "gpt-5.6-sol/xhigh\tgpt-5.6-terra/high\n",
            encoding="utf-8",
        )
        round_root = state / round_id / "teacher_rounds"
        packet = round_root / "iter_01" / "packet"
        packet.mkdir(parents=True)
        (packet / "baseline_artifacts.md").write_text(
            "### openroad_dpl_flow\n\n- HPWL_final: `100.0`\n",
            encoding="utf-8",
        )
        events = [
            {
                "time": "2026-07-25T10:00:00",
                "stage": "iteration",
                "message": "generate_done",
                "fields": {"iteration": "iter_01"},
            },
            {
                "time": "2026-07-25T10:00:01",
                "stage": "teacher",
                "message": "plan_done",
                "fields": {"iteration": "iter_01"},
            },
            {
                "time": "2026-07-25T10:00:02",
                "stage": "student",
                "message": "launch",
                "fields": {"iteration": "iter_01", "count": 4},
            },
        ]
        (round_root / "events.jsonl").write_text(
            "".join(json.dumps(event) + "\n" for event in events), encoding="utf-8"
        )
        operations = state / round_id / "checkpoints" / "operations"
        for student in range(1, 5):
            operation = operations / f"{round_id}_iter_01_student_{student:02d}"
            operation.mkdir(parents=True)
        return state, batch, round_id

    def test_dashboard_uses_real_artifacts_for_student_stages(self):
        with tempfile.TemporaryDirectory() as directory:
            state, batch, round_id = self.make_state(Path(directory))
            students = state / round_id / "teacher_rounds" / "students"

            artifact1 = students / "student_01" / "iter_01" / "artifacts"
            artifact1.mkdir(parents=True)
            (artifact1 / "implementation.diff").write_text("diff --git a/a b/a\n", encoding="utf-8")

            artifact2 = students / "student_02" / "iter_01" / "artifacts"
            write_json(artifact2 / "candidate_build_provenance.json", {"status": "complete"})

            artifact3 = students / "student_03" / "iter_01" / "artifacts"
            write_json(artifact3 / "candidate_evaluation_start.json", {"status": "started"})

            artifact4 = students / "student_04" / "iter_01" / "artifacts"
            write_json(
                artifact4 / "candidate_metrics_summary.json",
                {
                    "status": "ok",
                    "eligibility": {"eligible": True, "problems": []},
                    "canonical": {
                        "final_hpwl_micron": 95.0,
                        "runtime_seconds": 12.5,
                        "legality": "clean",
                    },
                },
            )

            view = dashboard.collect_view(
                state_root=state,
                batch_root=batch,
                case_id="aes_nangate45",
                students=4,
                iterations=2,
            )
            first = view.iterations[0]
            self.assertEqual(first.teacher_plan, dashboard.DONE)
            self.assertEqual(
                [(row.source, row.build, row.evaluate) for row in first.students],
                [
                    (dashboard.DONE, dashboard.RUN, dashboard.WAIT),
                    (dashboard.DONE, dashboard.DONE, dashboard.WAIT),
                    (dashboard.DONE, dashboard.DONE, dashboard.RUN),
                    # Metrics produced while the Student process is still
                    # active are provisional; self-repair may evaluate again.
                    (dashboard.DONE, dashboard.DONE, dashboard.RUN),
                ],
            )
            self.assertEqual(first.students[3].delta_percent, -5.0)
            self.assertTrue(
                all(row.source == dashboard.WAIT for row in view.iterations[1].students)
            )
            self.assertFalse(view.final)
            output = dashboard.render(
                view=view,
                case_id="aes_nangate45",
                teacher_model="gpt-5.6-sol",
                student_model="gpt-5.6-terra",
                elapsed_seconds=12,
                palette=dashboard.Palette(False),
                heartbeat_tick=2,
            )
            self.assertIn("Heartbeat -", output)
            self.assertIn("Current phase : Iteration 1 / parallel Students", output)
            self.assertIn("ETA           : learning Iteration 1 timings", output)
            self.assertIn("observable milestones", output)
            self.assertEqual(output.rstrip().splitlines()[-1].split()[0], "Doing")
            self.assertNotIn("Live activity", output)
            self.assertNotIn("              ->", output)

    def test_visible_tool_activity_is_reported_for_a_running_teacher(self):
        with tempfile.TemporaryDirectory() as directory:
            operation = Path(directory)
            events = [
                {
                    "type": "item.completed",
                    "item": {
                        "type": "command_execution",
                        "command": "sed -n '1,80p' /source/Optdp.cpp",
                    },
                },
                {
                    "type": "item.started",
                    "item": {
                        "type": "command_execution",
                        "command": "rg -n accept /source/detailed_global.cxx",
                    },
                },
            ]
            (operation / "codex_events.jsonl").write_text(
                "".join(json.dumps(event) + "\n" for event in events),
                encoding="utf-8",
            )
            activity = dashboard.operation_activity(operation)
            self.assertIn("1 tools completed", activity)
            self.assertIn("inspecting detailed_global.cxx", activity)
            self.assertIn("active now", activity)

    def test_final_snapshot_prints_candidate_and_output_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            state, batch, round_id = self.make_state(Path(directory))
            (batch / "status.tsv").write_text(
                "case\tstatus\tstart\tend\tlog\tround_id\n"
                f"aes_nangate45\tPASS\tstart\tend\tlog\t{round_id}\n",
                encoding="utf-8",
            )
            artifact = (
                state
                / round_id
                / "teacher_rounds/students/student_02/iter_01/artifacts"
            )
            artifact.mkdir(parents=True)
            (artifact / "implementation.diff").write_text("diff --git a/a b/a\n", encoding="utf-8")
            write_json(
                artifact / "candidate_metrics_summary.json",
                {
                    "status": "ok",
                    "eligibility": {"eligible": True, "problems": []},
                    "canonical": {
                        "final_hpwl_micron": 91.25,
                        "runtime_seconds": 8.0,
                        "legality": "clean",
                    },
                },
            )
            view = dashboard.collect_view(
                state_root=state,
                batch_root=batch,
                case_id="aes_nangate45",
                students=4,
                iterations=2,
            )
            output = dashboard.render(
                view=view,
                case_id="aes_nangate45",
                teacher_model="gpt-5.6-sol",
                student_model="gpt-5.6-terra",
                elapsed_seconds=61,
                palette=dashboard.Palette(False),
                launcher_log=batch / "demo-launcher.log",
            )
            self.assertTrue(view.final)
            self.assertIn("Best clean candidate", output)
            self.assertIn("91.2 um", output)
            self.assertIn("implementation.diff", output)
            self.assertIn("candidate_metrics_summary.json", output)
            self.assertIn("demo-launcher.log", output)
            self.assertIn("Final QoR", output)
            self.assertIn("Default HPWL", output)
            self.assertIn("Difference", output)
            self.assertIn("Protected-gate pass", output)
            self.assertIn("Top eligible candidates", output)

    def test_eta_uses_completed_first_iteration_operations(self):
        completed_students = tuple(
            dashboard.StudentView(
                student=index,
                iteration=1,
                source=dashboard.DONE,
                build=dashboard.DONE,
                evaluate=dashboard.DONE,
                worker_state=dashboard.DONE,
                worker_elapsed_seconds=100.0,
            )
            for index in range(1, 5)
        )
        waiting_students = tuple(
            dashboard.StudentView(student=index, iteration=2)
            for index in range(1, 5)
        )
        view = dashboard.DashboardView(
            round_id="round",
            batch_status="RUNNING",
            iterations=(
                dashboard.IterationView(
                    number=1,
                    teacher_plan=dashboard.DONE,
                    teacher_plan_activity="",
                    teacher_plan_elapsed_seconds=60.0,
                    students=completed_students,
                    teacher_review=dashboard.DONE,
                    teacher_review_activity="",
                    teacher_review_elapsed_seconds=30.0,
                ),
                dashboard.IterationView(
                    number=2,
                    teacher_plan=dashboard.WAIT,
                    teacher_plan_activity="",
                    teacher_plan_elapsed_seconds=None,
                    students=waiting_students,
                    teacher_review=dashboard.WAIT,
                    teacher_review_activity="",
                    teacher_review_elapsed_seconds=None,
                ),
            ),
            latest_event="review done",
            baseline_hpwl=100.0,
            final=False,
            failed=False,
            round_dir=None,
        )
        self.assertEqual(
            dashboard.adaptive_eta(view),
            "~00:03:10 remaining (adaptive, same-run samples)",
        )

    def test_demo_dry_run_assembles_requested_models_and_shape(self):
        result = subprocess.run(
            [
                "make",
                "--no-print-directory",
                "plan-demo-reviewdse",
                "DSE_RUN_PREFIX=unit_demo",
                "THREADS=3",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("gpt-5.6-sol", result.stdout)
        self.assertIn("gpt-5.6-terra", result.stdout)
        self.assertIn("xhigh", result.stdout)
        self.assertIn("case       : ariane133_nangate45", result.stdout)
        self.assertIn("--case ariane133_nangate45", result.stdout)
        self.assertIn("--start-kind framework", result.stdout)
        self.assertIn("--children 4", result.stdout)
        self.assertIn("--iterations 2", result.stdout)
        self.assertIn("--threads 3", result.stdout)
        self.assertIn("--run-prefix unit_demo", result.stdout)
        self.assertIn("--dry-run", result.stdout)

    def test_model_startup_failure_marks_later_stages_skipped_and_shows_cause(self):
        with tempfile.TemporaryDirectory() as directory:
            state, batch, round_id = self.make_state(Path(directory))
            operation = (
                state
                / round_id
                / "checkpoints/operations"
                / f"{round_id}_iter_01_student_01"
            )
            error = "The 'gpt-5.5-terra' model is not supported with this account."
            (operation / "codex_events.jsonl").write_text(
                json.dumps({"type": "error", "message": json.dumps({"detail": error})})
                + "\n"
                + json.dumps({"type": "turn.failed", "error": {"message": error}})
                + "\n",
                encoding="utf-8",
            )
            write_json(
                operation / "codex_usage_summary.json",
                {"returncode": 1, "elapsed_seconds": 1.5},
            )
            (batch / "status.tsv").write_text(
                "case\tstatus\tstart\tend\tlog\tround_id\n"
                f"aes_nangate45\tFAIL(1)\tstart\tend\tlog\t{round_id}\n",
                encoding="utf-8",
            )

            view = dashboard.collect_view(
                state_root=state,
                batch_root=batch,
                case_id="aes_nangate45",
                students=4,
                iterations=2,
            )
            first = view.iterations[0].students[0]
            self.assertEqual(
                (first.source, first.build, first.evaluate),
                (dashboard.FAIL, dashboard.SKIP, dashboard.SKIP),
            )
            self.assertEqual(view.root_cause, error)
            output = dashboard.render(
                view=view,
                case_id="aes_nangate45",
                teacher_model="gpt-5.6-sol",
                student_model="gpt-5.6-terra",
                elapsed_seconds=10,
                palette=dashboard.Palette(False),
            )
            self.assertIn(f"Doing         : {error}", output)
            self.assertIn("FAIL  SKIP  SKIP", output)
            self.assertEqual(output.rstrip().splitlines()[-1], f"Doing         : {error}")


if __name__ == "__main__":
    unittest.main()
