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

    async def test_remote_execution_requires_an_explicit_repository_root(self):
        config = server.SSHConfig(host="eda.example.edu", root_path="")
        probe = await server.ssh_connect(config)
        self.assertFalse(probe["success"])
        self.assertIn("Repository path is required", probe["message"])

        with self.assertRaisesRegex(Exception, "Repository path is required"):
            await server.run_named_task("doctor", server.RunRequest(ssh=config))

    def test_core_tasks_are_fixed_commands(self):
        self.assertEqual(server.TASKS["doctor"][1], ["make", "doctor"])
        self.assertEqual(server.TASKS["bootstrap"][1], ["make", "bootstrap"])
        self.assertEqual(server.TASKS["build-tools"][1], ["make", "build-tools"])
        self.assertEqual(server.TASKS["check"][1], ["make", "check"])
        self.assertEqual(server.TASKS["prepare-inputs"][1], ["make", "prepare-paper-inputs"])
        self.assertEqual(server.TASKS["fetch-table6-data"][1], ["make", "fetch-table6-data"])
        self.assertEqual(server.TASKS["table5-data-check"][1], ["make", "check-table5-data"])
        self.assertEqual(server.TASKS["table4-fresh"][1], ["make", "reproduce-table4"])
        self.assertEqual(server.TASKS["table5-fresh"][1], ["make", "reproduce-table5"])
        self.assertEqual(server.TASKS["table6-fresh"][1], ["make", "reproduce-table6"])
        self.assertEqual(server.TASKS["figures"][1], ["make", "reproduce-figures"])
        self.assertEqual(
            server.TASKS["ariane-diagnostic"][1],
            ["make", "reproduce-ariane-diagnostic"],
        )
        self.assertEqual(
            server.TASKS["search-paper"][1],
            ["make", "reproduce-paper-search", "ACKNOWLEDGE_LLM_COST=yes"],
        )

    def test_ui_is_a_two_stage_reviewer_console(self):
        html = (WEB_DEMO_ROOT / "templates" / "index.html").read_text(encoding="utf-8")
        self.assertIn("1. Prepare the Environment and Inputs", html)
        self.assertIn("2. Run the Paper Experiments", html)
        self.assertIn("prepare-paper-inputs", html)
        self.assertIn("fetch-table6-data", html)
        self.assertIn("reproduce-table4", html)
        self.assertIn("reproduce-table5", html)
        self.assertIn("reproduce-table6", html)
        self.assertIn("reproduce-paper-search", html)
        self.assertIn("reproduce-ariane-diagnostic", html)
        self.assertIn("paper-scale token budget", html)
        self.assertNotIn('data-task="reviewer-aes"', html)
        self.assertNotIn('class="path-grid"', html)
        self.assertNotIn('id="view-slides"', html)
        self.assertNotIn('id="view-guide"', html)

    def test_ui_exposes_every_fixed_reviewer_task(self):
        html = (WEB_DEMO_ROOT / "templates" / "index.html").read_text(encoding="utf-8")
        ui_tasks = set(re.findall(r'data-task="([^"]+)"', html))
        self.assertEqual(ui_tasks, set(server.TASKS))


if __name__ == "__main__":
    unittest.main()
