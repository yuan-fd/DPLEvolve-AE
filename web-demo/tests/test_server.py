import asyncio
import re
import sys
import unittest
from pathlib import Path


WEB_DEMO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(WEB_DEMO_ROOT))

import server  # noqa: E402


class ServerRegressionTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        server.RUN_STATE.update(
            {
                "running": False,
                "current_command": "",
                "start_time": None,
                "exit_code": None,
                "history": [],
                "process": None,
                "ssh_channel": None,
            }
        )
        server._cmd_queue = asyncio.Queue()
        server._queue_lock = asyncio.Lock()
        server._queued_jobs.clear()
        server._cancelled_job_ids.clear()
        server._job_id_counter = 0

    async def test_failure_report_does_not_stop_worker(self):
        worker = asyncio.create_task(server._queue_worker())
        try:
            first = await server._enqueue(
                "Expected Failure", ["bash", "-c", "echo 'missing file' >&2; exit 7"]
            )
            second = await server._enqueue(
                "Recovery Command", ["bash", "-c", "exit 0"]
            )
            await asyncio.wait_for(server._cmd_queue.join(), timeout=5)

            self.assertFalse(first["queued"])
            self.assertTrue(second["queued"])
            self.assertGreaterEqual(second["position"], 1)
            self.assertEqual([item["exit_code"] for item in server.RUN_STATE["history"]], [7, 0])
            self.assertFalse(worker.done())
        finally:
            worker.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await worker

    async def test_waiting_job_can_be_removed(self):
        first = await server._enqueue("First", ["bash", "-c", "exit 0"])
        second = await server._enqueue("Second", ["bash", "-c", "exit 0"])
        result = await server.cancel_queued_job(second["job_id"])
        self.assertTrue(result["cancelled"])

        worker = asyncio.create_task(server._queue_worker())
        try:
            await asyncio.wait_for(server._cmd_queue.join(), timeout=5)
            self.assertEqual(first["job_id"], "job-1")
            self.assertEqual([item["command"] for item in server.RUN_STATE["history"]], ["First"])
        finally:
            worker.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await worker

    async def test_active_local_process_group_can_be_cancelled(self):
        command = asyncio.create_task(
            server.run_command(["bash", "-c", "sleep 30"], cwd=WEB_DEMO_ROOT, label="Sleep")
        )
        for _ in range(100):
            if server.RUN_STATE.get("process") is not None:
                break
            await asyncio.sleep(0.01)

        result = await server.cancel_run()
        exit_code = await asyncio.wait_for(command, timeout=5)

        self.assertTrue(result["cancelled"])
        self.assertEqual(exit_code, -15)
        self.assertFalse(server.RUN_STATE["running"])
        self.assertEqual(server.RUN_STATE["history"][-1]["exit_code"], -15)

    def test_failure_patterns_are_available(self):
        report = server._analyze_failure(127, ["make: command not found"])
        self.assertEqual(report["exit_code"], 127)
        self.assertTrue(any("GNU Make" in item for item in report["suggestions"]))

    def test_unknown_failure_points_reviewer_to_doctor(self):
        report = server._analyze_failure(1, ["unexpected tool failure"])
        self.assertTrue(any("make doctor" in item for item in report["suggestions"]))
        self.assertTrue(any("does not install" in item for item in report["suggestions"]))

    def test_core_tasks_are_fixed_commands(self):
        self.assertEqual(server.TASKS["doctor"][1], ["bash", "scripts/human/doctor.sh"])
        self.assertEqual(
            server.TASKS["validate-evaluator"][1],
            ["make", "validate-evaluator"],
        )
        self.assertEqual(server.TASKS["audit-archive"][1], ["make", "audit-archive"])
        self.assertEqual(server.TASKS["fetch-table6-data"][1], ["make", "fetch-table6-data"])
        self.assertEqual(server.TASKS["prepare-table5-inputs"][1], ["make", "prepare-table5-inputs"])

    def test_full_reproduction_uses_documented_order(self):
        self.assertEqual(
            server.TASKS["full"][1],
            [
                "bash",
                "-c",
                "make bootstrap && make build-tools && make prepare-paper-inputs && make validate-evaluator",
            ],
        )

    def test_ui_explains_fresh_clone_paths(self):
        html = (WEB_DEMO_ROOT / "templates" / "index.html").read_text(encoding="utf-8")
        self.assertIn("Fresh clone: prepare the experiment", html)
        self.assertIn("prepare-paper-inputs", html)
        self.assertIn("reproduce-bo", html)
        self.assertIn("six missing candidate sources", html)
        self.assertIn("config_dense2.mk", html)
        self.assertIn("does not need the deleted ODBs", html)
        self.assertIn("archive audit is clearly separated", html)

    def test_ui_exposes_every_fixed_reviewer_task(self):
        html = (WEB_DEMO_ROOT / "templates" / "index.html").read_text(encoding="utf-8")
        ui_tasks = set(re.findall(r'data-task="([^"]+)"', html))
        self.assertEqual(ui_tasks, set(server.TASKS))


if __name__ == "__main__":
    unittest.main()
