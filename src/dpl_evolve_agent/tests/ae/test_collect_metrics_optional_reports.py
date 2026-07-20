from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


AGENT_ROOT = Path(__file__).resolve().parents[2]
COLLECT_METRICS = AGENT_ROOT / "baseline" / "collect_metrics.py"


class CollectMetricsOptionalReportTests(unittest.TestCase):
    def test_missing_requested_reports_are_explicitly_absent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            flow_home = Path(tmp)
            report_dir = flow_home / "reports" / "case"
            result_dir = flow_home / "results" / "case"
            report_dir.mkdir(parents=True)
            result_dir.mkdir(parents=True)

            snapshot = "inst\tx\ty\tis_fixed\ncell0\t0\t0\t0\n"
            before = result_dir / "before.tsv"
            after = result_dir / "after.tsv"
            before.write_text(snapshot, encoding="utf-8")
            after.write_text(snapshot, encoding="utf-8")
            run_summary = report_dir / "legalize_summary.json"
            run_summary.write_text('{"dbu_per_micron": 2000}\n', encoding="utf-8")
            post_summary = report_dir / "post_metrics_summary.json"
            post_summary.write_text(
                json.dumps(
                    {
                        "placement_violations": "",
                        "check_report": "reports/case/check_placement_report.json",
                    }
                ),
                encoding="utf-8",
            )
            output = report_dir / "metrics.json"

            command = [
                sys.executable,
                str(COLLECT_METRICS),
                "--before",
                str(before),
                "--after",
                str(after),
                "--run-summary",
                str(run_summary),
                "--post-metrics-summary",
                str(post_summary),
                "--detailed-placement-report",
                str(report_dir / "detailed_placement_report.json"),
                "--output",
                str(output),
            ]
            env = os.environ.copy()
            env["FLOW_HOME"] = str(flow_home)
            subprocess.run(command, check=True, env=env)

            metrics = json.loads(output.read_text(encoding="utf-8"))
            self.assertIsNone(metrics["legality"]["check_report"])
            self.assertEqual(metrics["legality"]["check_report_status"], "absent")
            self.assertEqual(
                metrics["optional_artifacts"]["check_placement_report_json"],
                {"status": "absent"},
            )
            self.assertEqual(
                metrics["optional_artifacts"]["detailed_placement_report_json"],
                {"status": "absent"},
            )
            self.assertNotIn("check_placement_report_json", metrics.get("supporting_files", {}))
            self.assertNotIn("detailed_placement_report_json", metrics.get("supporting_files", {}))


if __name__ == "__main__":
    unittest.main()
