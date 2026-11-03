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
    def test_handoff_requires_received_version_and_preserved_primary(self):
        published=("coop camera transport: seq={seq} camera_seq={seq} target=99 "
                   "generation=3221225473 active=true primary_unchanged=true secondary_matches=true "
                   "position=12,20,30 projection=0.5,18000,1.2,1.7777778")
        consumed=("coop camera packet: seq={seq} stage=consume present=240 readable=true finite=true "
                  "transform=1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,0 "
                  "projection=0.5,18000,1.2,1.7777778 viewport=00000000,050002D0 "
                  "secondary_flags=C0000001 secondary_position=12,20,30 "
                  "secondary_projection=0.5,18000,1.2,1.7777778 secondary_viewport=00000000,050002D0")
        good=trace()+[published.format(seq=i) for i in range(1,4)]+[consumed.format(seq=i) for i in (240,360,480)]
        self.assertTrue(reporter.analyze(good,required_handoff=True)["passed"])
        self.assertFalse(reporter.analyze(trace(),required_handoff=True)["passed"])
        self.assertFalse(reporter.analyze([s.replace('primary_unchanged=true','primary_unchanged=false') for s in good],required_handoff=True)["passed"])
        self.assertFalse(reporter.analyze([s.replace('secondary_flags=C0000001','secondary_flags=C0000002') for s in good],required_handoff=True)["passed"])

    def test_handoff_checks_each_leave_and_rejoin_interval(self):
        def publish(version, sequence):
            return (f"coop camera transport: seq={sequence} generation={version} "
                    f"active={'true' if version else 'false'} primary_unchanged=true "
                    "secondary_matches=true position=12,20,30")

        def consume(version, sequence):
            row = trace()[3].replace("seq=240", f"seq={sequence}")
            position = "12,20,30" if version else "0,0,0"
            projection = "0.5,18000,1.2,1.7777778" if version else "0,0,0,0"
            viewport = "00000000,050002D0" if version else "00000000,00000000"
            return (f"{row} secondary_flags={version:08X} secondary_position={position} "
                    f"secondary_projection={projection} secondary_viewport={viewport}")

        v1, v2 = 0xC0000001, 0xC0000002
        initial = trace() + [consume(0, 230), publish(v1, 1), consume(v1, 240),
                             publish(v1, 2), consume(v1, 360), publish(v1, 3), consume(v1, 480)]
        good = initial + [publish(0, 4), consume(0, 600), publish(v2, 5), consume(v2, 720)]
        self.assertTrue(reporter.analyze(good, required_handoff=True)["passed"])
        bad_clear = initial + [publish(0, 4), consume(v1, 600), publish(v2, 5), consume(v2, 720)]
        self.assertFalse(reporter.analyze(bad_clear, required_handoff=True)["passed"])
        stale_rejoin = initial + [publish(0, 4), consume(0, 600), publish(v2, 5), consume(v1, 720)]
        self.assertFalse(reporter.analyze(stale_rejoin, required_handoff=True)["passed"])
        # Even the right new version must carry the new camera, not stale coordinates.
        wrong_position = good[:-1] + [good[-1].replace("secondary_position=12,20,30", "secondary_position=90,20,30")]
        self.assertFalse(reporter.analyze(wrong_position, required_handoff=True)["passed"])
        dirty_clear = [s.replace("secondary_position=0,0,0", "secondary_position=1,0,0")
                       if "seq=600 " in s else s for s in good]
        self.assertFalse(reporter.analyze(dirty_clear, required_handoff=True)["passed"])

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
