#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FREEZER = ROOT / "scripts/reproduce/freeze_level1.py"
VERIFIER = ROOT / "scripts/reproduce/verify_level1.py"
CASES = {
    "jpeg_util90_nangate45": "paper_level1_jpeg_util90",
    "aes_nangate45": "paper_level1_aes_util70",
    "swerv_wrapper_nangate45": "paper_level1_swerv_util60",
}


class Level1FreezeTests(unittest.TestCase):
    def prepare_state(self, root: Path, children: int = 2) -> list[str]:
        for name in (
            "framework_dpl_evolve",
            "diamond_dpl_evolve",
            "default_negotiation_dpl_evolve",
        ):
            source = root / "seed_sources" / name
            source.mkdir(parents=True)
            (source / "CMakeLists.txt").write_text("project(test)\n")
            (source / "source.cpp").write_text(f"// {name}\n")
            (source / ".git").mkdir()
            (source / ".git" / "mutable").write_text("must not enter tree hash\n")

        rounds = []
        for index, (case, flow_variant) in enumerate(CASES.items(), 1):
            round_id = f"paper_level1_test_{index}"
            rounds.append(round_id)
            round_root = root / round_id
            teacher_rounds = round_root / "teacher_rounds"
            teacher_rounds.mkdir(parents=True)
            seed_manifest = round_root / "start_seed_calibration" / "manifest.tsv"
            seed_manifest.parent.mkdir(parents=True)
            seed_manifest.write_text("start\tstatus\nframework\tPASS\n")
            child_rows = []
            for child_index in range(1, children + 1):
                student_id = f"student_{child_index:02d}"
                operation_id = f"{round_id}_iter_01_{student_id}"
                invocation = round_root / "checkpoints" / "operations" / operation_id / "codex_invocation.json"
                invocation.parent.mkdir(parents=True)
                invocation.write_text(json.dumps({"operation_id": operation_id}))
                artifacts = teacher_rounds / "students" / student_id / "iter_01" / "artifacts"
                artifacts.mkdir(parents=True)
                (artifacts / "knowledge_card.md").write_text("# Student evidence\n")
                (artifacts / "source_commit.json").write_text('{"commit":"abc"}\n')
                (artifacts / "candidate_metrics_summary.json").write_text('{"status":"ok"}\n')
                child_rows.append({"student_id": student_id, "operation_id": operation_id})

            review_operation = f"{round_id}_iter_01_teacher_review"
            review_dir = round_root / "checkpoints" / "operations" / review_operation
            review_dir.mkdir(parents=True)
            (review_dir / "codex_invocation.json").write_text(
                json.dumps({"operation_id": review_operation})
            )
            (review_dir / "codex_last_message.txt").write_text(
                "Calibration gate: complete.\n\nKnowledge synthesis: reviewed evidence.\n"
            )
            manifest = {
                "round_id": round_id,
                "case": case,
                "flow_variant": flow_variant,
                "start_kind": "framework",
                "iterations": [{
                    "iteration": "iter_01",
                    "calibration_mode": True,
                    "children": child_rows,
                    "teacher_review_operation": review_operation,
                }],
                "teacher_model": "gpt-5.5",
                "teacher_reasoning_effort": "xhigh",
                "student_model": "gpt-5.4",
                "student_reasoning_effort": "xhigh",
                "student_runtime_multiplier": 2.0,
                "calibration_mode": True,
                "dry_run": False,
                "launch": True,
            }
            (teacher_rounds / "manifest.json").write_text(json.dumps(manifest))
        return rounds

    def run_freezer(self, state: Path, rounds: list[str], packet: Path, children: int = 2):
        command = [
            sys.executable, str(FREEZER), "--state-root", str(state),
            "--children-per-case", str(children), "--output", str(packet),
        ]
        for round_id in rounds:
            command.extend(["--round", round_id])
        return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def test_freezes_actual_teacher_outputs_and_verifies_packet(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "state"
            rounds = self.prepare_state(state)
            packet = Path(directory) / "level1_evidence.md"
            result = self.run_freezer(state, rounds, packet)
            self.assertEqual(result.returncode, 0, result.stdout)
            manifest = json.loads(packet.with_suffix(".json").read_text())
            self.assertEqual(manifest["status"], "complete_fresh_public_reconstruction")
            self.assertFalse(manifest["author_time_level1_packet_retained"])
            self.assertEqual(len(manifest["rounds"]), 3)
            source_hashes = [row["tree_sha256"] for row in manifest["source_starts"]]
            for git_noise in state.glob("seed_sources/*/.git/mutable"):
                git_noise.write_text("changed Git bookkeeping\n")
            second_packet = Path(directory) / "level1_evidence_second.md"
            second = self.run_freezer(state, rounds, second_packet)
            self.assertEqual(second.returncode, 0, second.stdout)
            second_manifest = json.loads(second_packet.with_suffix(".json").read_text())
            self.assertEqual(
                source_hashes,
                [row["tree_sha256"] for row in second_manifest["source_starts"]],
            )
            verify = subprocess.run(
                [sys.executable, str(VERIFIER), "--packet", str(packet)],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
            self.assertEqual(verify.returncode, 0, verify.stdout)

            packet.write_text(packet.read_text() + "tampered\n")
            rejected = subprocess.run(
                [sys.executable, str(VERIFIER), "--packet", str(packet)],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("SHA-256", rejected.stdout)

    def test_refuses_prompt_without_real_teacher_final_review(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "state"
            rounds = self.prepare_state(state)
            missing = (
                state / rounds[0] / "checkpoints" / "operations"
                / f"{rounds[0]}_iter_01_teacher_review" / "codex_last_message.txt"
            )
            missing.unlink()
            prompt = state / rounds[0] / "teacher_rounds" / "iter_01" / "prompts" / "teacher_review.md"
            prompt.parent.mkdir(parents=True)
            prompt.write_text("Calibration gate prompt; knowledge synthesis prompt.\n")
            result = self.run_freezer(state, rounds, Path(directory) / "packet.md")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Teacher final review", result.stdout)


if __name__ == "__main__":
    unittest.main()
