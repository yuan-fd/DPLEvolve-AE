import json
import tempfile
import unittest
from pathlib import Path

from scripts.maintenance.summarize_token_usage import aggregate


class TestTokenAggregation(unittest.TestCase):
    def test_aggregates_cached_and_active_tokens(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            campaign = root / (
                "evolve_9case_20260517_t55x_s54x_4x15_rerun1_"
                "place_batch_20260421_220319_aes_nangate45_4x15_models"
            )
            operation = campaign / "checkpoints" / "operations" / "op1"
            operation.mkdir(parents=True)
            (operation / "codex_usage_summary.json").write_text(
                json.dumps(
                    {
                        "returncode": 0,
                        "usage": {
                            "input_tokens": 100,
                            "cached_input_tokens": 70,
                            "output_tokens": 20,
                        },
                    }
                ),
                encoding="utf-8",
            )

            rows = aggregate(root, "evolve_9case_20260517_t55x_s54x_4x15_rerun1_")

        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["case"], "aes_nangate45")
        self.assertEqual(rows[0]["session_active_tokens"], 50)
        self.assertEqual(rows[0]["session_logged_tokens"], 120)

    def test_accepts_ten_iteration_campaign(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            campaign = root / (
                "rerun_9case_width4x10_parallel3_"
                "place_batch_20260421_220319_jpeg_asap7_4x10_models"
            )
            operation = campaign / "checkpoints" / "operations" / "op1"
            operation.mkdir(parents=True)
            (operation / "codex_usage_summary.json").write_text(
                json.dumps({"usage": {"input_tokens": 10, "output_tokens": 2}}),
                encoding="utf-8",
            )

            rows = aggregate(root, "rerun_9case_width4x10_parallel3_")

        self.assertEqual(rows[0]["case"], "jpeg_asap7")
        self.assertEqual(rows[0]["session_logged_tokens"], 12)

    def test_deduplicates_cumulative_session_snapshots(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            campaign = root / (
                "rerun_9case_width4x10_parallel3_"
                "place_batch_20260421_220319_aes_asap7_4x10_models"
            )
            for iteration, input_tokens in ((1, 100), (2, 180)):
                operation = campaign / "checkpoints" / "operations" / f"op{iteration}"
                operation.mkdir(parents=True)
                (operation / "codex_usage_summary.json").write_text(
                    json.dumps(
                        {
                            "thread_id": "persistent-thread",
                            "usage": {
                                "input_tokens": input_tokens,
                                "cached_input_tokens": 80,
                                "output_tokens": 20,
                            },
                        }
                    ),
                    encoding="utf-8",
                )

            rows = aggregate(root, "rerun_9case_width4x10_parallel3_")

        self.assertEqual(rows[0]["operations"], 2)
        self.assertEqual(rows[0]["sessions"], 1)
        self.assertEqual(rows[0]["snapshot_logged_tokens"], 320)
        self.assertEqual(rows[0]["session_logged_tokens"], 200)


if __name__ == "__main__":
    unittest.main()
