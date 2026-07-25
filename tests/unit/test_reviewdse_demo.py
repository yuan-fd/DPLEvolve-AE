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
        round_id = "demo_paper9_place_aes_nangate45_4x2_tgpt56sol_xhigh_sgpt55terra_xhigh"
        (batch / "experiments.tsv").write_text(
            "case\tflow_variant\tround_id\tstart_kind\tchildren\titerations\tteacher\tstudent\n"
            f"aes_nangate45\tpaper9_place\t{round_id}\tframework\t4\t2\t"
            "gpt-5.6-sol/xhigh\tgpt-5.5-terra/xhigh\n",
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
                    (dashboard.DONE, dashboard.DONE, dashboard.DONE),
                ],
            )
            self.assertEqual(first.students[3].delta_percent, -5.0)
            self.assertFalse(view.final)

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
                student_model="gpt-5.5-terra",
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
        self.assertIn("gpt-5.5-terra", result.stdout)
        self.assertIn("--children 4", result.stdout)
        self.assertIn("--iterations 2", result.stdout)
        self.assertIn("--threads 3", result.stdout)
        self.assertIn("--run-prefix unit_demo", result.stdout)
        self.assertIn("--dry-run", result.stdout)


if __name__ == "__main__":
    unittest.main()
