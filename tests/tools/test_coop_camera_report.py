import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "coop_camera_report", Path(__file__).resolve().parents[2] / "tools/analysis/coop_camera_report.py")
reporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reporter)

START = "coop: single-instance input preparation;"


def trace():
    # Independent producer/render ordinals: render can consume an older packet.
    transform = "1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,0"
    packet = (f"readable=true finite=true transform={transform} "
              "projection=0.5,18000,1.2,1.7777778 viewport=00000000,050002D0")
    return [START,
            "coop camera update: seq=120 target=1 readable=true position=10,20,30",
            f"coop camera packet: seq=120 stage=submit present=200 {packet}",
            f"coop camera packet: seq=240 stage=consume present=240 {packet}",
            "coop camera apply: seq=240 present=240 view=0 readable=true position=10,20,30"]


class CameraEvidence(unittest.TestCase):
    def test_second_camera_requires_distinct_owner_and_target(self):
        second = ("coop second camera: seq={seq} owner=01000000 primary=02000000 "
                  "target=99 primary_target=1 singleton_unchanged=true finite=true "
                  "position=12,20,30 primary_position=10,20,30")
        good = trace() + [second.format(seq=i) for i in range(1, 4)]
        self.assertTrue(reporter.analyze(good, required_secondary=True)["passed"])
        bad = [line.replace("owner=01000000", "owner=02000000") for line in good]
        self.assertFalse(reporter.analyze(bad, required_secondary=True)["passed"])
        bad = [line.replace("primary_target=1", "primary_target=99") for line in good]
        self.assertFalse(reporter.analyze(bad, required_secondary=True)["passed"])
        self.assertFalse(reporter.analyze(trace(), required_secondary=True)["passed"])

    def test_cli_reads_rotated_segments_in_chronological_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            # Startup and producer samples have rotated out of the active file.
            (root / "probe.2.log").write_text(START + "\nUnhandled guest access violation\n")
            (root / "probe.1.log").write_text("\n".join(trace()[:3]) + "\n")
            (root / "probe.log").write_text("\n".join(trace()[3:]) + "\n")
            output = root / "report.json"
            result = subprocess.run([sys.executable, str(Path(reporter.__file__)),
                                     "--log", str(root / "probe.log"), "--output", str(output),
                                     "--require-target", "1"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(json.loads(output.read_text())["passed"])

    def test_checks_positions_at_both_boundaries(self):
        self.assertTrue(reporter.analyze(trace(), [1])["passed"])
        broken = trace()
        broken[-1] = broken[-1].replace("position=10,20,30", "position=40,20,30")
        self.assertFalse(reporter.analyze(broken)["passed"])

    def test_latest_start_discards_previous_success(self):
        self.assertFalse(reporter.analyze(trace() + [START], [1])["passed"])

    def test_required_target_must_have_matching_submission(self):
        lines = trace() + [
            "coop camera update: seq=360 target=99 readable=true position=90,20,30"]
        self.assertFalse(reporter.analyze(lines, [99])["passed"])

    def test_malformed_target_is_reported_without_crashing(self):
        lines = [line.replace("target=1", "target=invalid") for line in trace()]
        self.assertFalse(reporter.analyze(lines)["passed"])

    def test_unreadable_and_nonfinite_samples_fail(self):
        for old, new in (("readable=true", "readable=false"), ("10,20,30", "nan,20,30")):
            with self.subTest(new=new):
                self.assertFalse(reporter.analyze([line.replace(old, new) for line in trace()])["passed"])


if __name__ == "__main__":
    unittest.main()
