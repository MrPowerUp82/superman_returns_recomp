"""tools/analysis/frame_timeline_report.py on synthetic timeline CSVs."""
import json

import pytest

import frame_timeline_report as ftr

HEADER = "frame,stage,begin_ns,end_ns,busy_ns,blocked_ns\n"
MS = 1_000_000


def write_csv(path, rows):
    path.write_text(HEADER + "".join(f"{f},{s},{b},{e},{busy},{blk}\n" for f, s, b, e, busy, blk in rows))


def frame_rows(frame, interval_ms=33.3, skip=()):
    end = 1_000 * MS + round(frame * interval_ms * MS)
    rows = [
        (frame, "game", end - 20 * MS, end, 20 * MS, 0),
        (frame, "capture", 0, 0, 6 * MS, 0),
        (frame, "front_wait", end, end + 2 * MS, 2 * MS, 0),
        (frame, "worker", end, end + 28 * MS, 25 * MS, 3 * MS),
        (frame, "record", end, end + 33 * MS, 30 * MS, 3 * MS),
        (frame, "gpu", 0, 0, 18 * MS, 0),
    ]
    return [row for row in rows if row[1] not in skip]


def make(tmp_path, frames=10, skip_by_frame=None):
    skip_by_frame = skip_by_frame or {}
    rows = []
    for frame in range(1, frames + 1):
        rows += frame_rows(frame, skip=skip_by_frame.get(frame, ()))
    path = tmp_path / "timeline.csv"
    write_csv(path, rows)
    return path


def test_interval_and_fps(tmp_path):
    result = ftr.analyze(ftr.load(make(tmp_path)))
    assert result["frames"] == 10
    assert result["dropped"] == 0
    assert result["interval"]["samples"] == 9
    assert result["interval"]["mean_ms"] == pytest.approx(33.3, abs=0.01)
    assert result["interval"]["mean_fps"] == pytest.approx(30.03, rel=0.01)
    assert result["interval"]["low1_fps"] == pytest.approx(30.03, rel=0.01)


def test_stage_stats(tmp_path):
    stages = ftr.analyze(ftr.load(make(tmp_path)))["stages"]
    assert stages["worker"]["mean"] == pytest.approx(25.0)
    assert stages["worker"]["blocked_mean"] == pytest.approx(3.0)
    assert stages["worker"]["util"] == pytest.approx(25.0 / 33.3, rel=0.01)
    assert stages["worker"]["headroom_p99_ms"] == pytest.approx(1000 / 30 - 25.0, rel=0.01)
    assert stages["game_other"]["mean"] == pytest.approx(14.0)  # game 20 - capture 6


def test_limiting_stage_is_the_largest_busy_time(tmp_path):
    share = ftr.analyze(ftr.load(make(tmp_path)))["limiting_share"]
    assert share == {"game": 0.0, "worker": 0.0, "record": 1.0, "gpu": 0.0}


def test_frames_missing_a_required_stage_are_dropped(tmp_path):
    path = make(tmp_path, skip_by_frame={5: ("worker",)})
    result = ftr.analyze(ftr.load(path))
    assert result["frames"] == 9
    assert result["dropped"] == 1
    # pairs (1,2) (2,3) (3,4) and (6,7) (7,8) (8,9) (9,10): the gap breaks two pairs
    assert result["interval"]["samples"] == 7


def test_last_limits_the_window(tmp_path):
    result = ftr.analyze(ftr.load(make(tmp_path)), last=4)
    assert result["frames"] == 4


def test_api_without_record_and_capture_stages(tmp_path):
    rows = []
    for frame in range(1, 6):
        rows += frame_rows(frame, skip=("record", "capture"))
    path = tmp_path / "d3d12.csv"
    write_csv(path, rows)
    result = ftr.analyze(ftr.load(path))
    assert "record" not in result["stages"]
    assert "game_other" not in result["stages"]
    assert set(result["limiting_share"]) == {"game", "worker", "gpu"}
    assert result["limiting_share"]["worker"] == 1.0


def test_percentile_nearest_rank():
    assert ftr.percentile(list(range(1, 101)), 99) == 99
    assert ftr.percentile([1, 2, 3, 4], 50) == 2
    assert ftr.percentile([], 99) == 0.0


def test_cli_prints_the_table_and_json(tmp_path, capsys):
    path = make(tmp_path)
    assert ftr.main([str(path)]) == 0
    text = capsys.readouterr().out
    assert "limitante" in text and "record" in text and "1% low" in text
    assert ftr.main([str(path), "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert "stages" in data and data["frames"] == 10


def test_cli_fails_when_no_frame_is_complete(tmp_path):
    path = tmp_path / "empty.csv"
    write_csv(path, [])
    assert ftr.main([str(path)]) == 1
