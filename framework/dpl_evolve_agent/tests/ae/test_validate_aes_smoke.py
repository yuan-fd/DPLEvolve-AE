from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


AGENT_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(AGENT_ROOT / "scripts" / "ae"))

from validate_aes_smoke import validate  # noqa: E402


class ValidateAesSmokeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.lock = json.loads(
            (AGENT_ROOT / "metadata" / "ae_reproduction_lock.json").read_text(
                encoding="utf-8"
            )
        )
        expected = self.lock["aes_nangate45_smoke"]["expected"]
        self.metrics = {
            "status": "ok",
            "legalize_exit_status": 0,
            "design_metrics": {
                "instance_count": expected["instance_count"],
                "instance_area": expected["instance_area_micron2"],
            },
            "hpwl_stages": {
                "global_micron": expected["global_hpwl_micron"],
                "final_micron": expected["final_hpwl_micron"],
            },
            "metrics_stage": {"error_count": 0},
            "legality": {"placement_violations": ""},
        }

    def test_valid_artifacts_pass(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            odb = Path(tmp) / "input.odb"
            odb.write_bytes(b"pinned test odb")
            self.lock["aes_nangate45_smoke"]["input_odb_sha256"] = hashlib.sha256(
                odb.read_bytes()
            ).hexdigest()
            self.assertEqual(validate(self.lock, odb, self.metrics), [])

    def test_hpwl_drift_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            odb = Path(tmp) / "input.odb"
            odb.write_bytes(b"pinned test odb")
            self.lock["aes_nangate45_smoke"]["input_odb_sha256"] = hashlib.sha256(
                odb.read_bytes()
            ).hexdigest()
            self.metrics["hpwl_stages"]["final_micron"] += 1.0
            failures = validate(self.lock, odb, self.metrics)
            self.assertTrue(any("final_hpwl_micron" in item for item in failures))


if __name__ == "__main__":
    unittest.main()
