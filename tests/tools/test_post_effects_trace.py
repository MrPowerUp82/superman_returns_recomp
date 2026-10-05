"""tools/bench/post_effects_trace.py on a synthetic raw trace."""
import csv

import post_effects_trace as pt

FIELDS = ["frame", "event", "kind", "accepted", "primitive", "index_count", "index_format",
          "vs_hash", "ps_hash", "rb_mode", "rb_surface", "rb_color", "rb_depth",
          "rb_color_mask", "rb_copy"]
SCENE = 335545600
SMALL = 83886400  # pitch 320


def row(frame, event, kind, prim, ps, mode, surface, copy=0):
    return {"frame": frame, "event": event, "kind": kind, "accepted": 0 if "skip" in kind else 1,
            "primitive": prim, "index_count": 4, "index_format": 0,
            "vs_hash": "871a3860c63cb28d", "ps_hash": ps, "rb_mode": mode,
            "rb_surface": surface, "rb_color": 0, "rb_depth": 0, "rb_color_mask": 0,
            "rb_copy": copy}


def write(path, rows):
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, FIELDS)
        w.writeheader()
        w.writerows(rows)


def frame_rows(frame, base, clear=0):
    return [
        row(frame, base, "draw", 4, "aaaaaaaaaaaaaaaa", 4, SCENE),
        row(frame, base + 1, "draw", 4, "aaaaaaaaaaaaaaaa", 4, SCENE),
        row(frame, base + 2, "skipped", 13, "1e70eb9513d670c9", 4, SMALL),
        row(frame, base + 3, "skipped_copy", 0, "1e70eb9513d670c9", 6, SMALL, clear),
        row(frame, base + 4, "draw", 8, "1e70eb9513d670c9", 6, SMALL),
        row(frame, base + 5, "draw", 13, "2679c172f83de85d", 4, SCENE),
        row(frame, base + 6, "swap", 0, "2679c172f83de85d", 6, SCENE),
    ]


def test_summary_counts_and_order(tmp_path):
    p = tmp_path / "t.csv"
    write(p, frame_rows(0, 0) + frame_rows(1, 7, clear=1 << 8))
    lines, total = pt.summarize(pt.read(p), 1)
    text = "\n".join(lines)
    assert total == 4
    assert "skipped       per frame: min 1 max 1 total 2" in text
    assert "WARNING" in text and "1E70EB9513D670C9   320 color: 1" in text
    order = text[text.index("frame 1 order"):]
    assert order.index("other draws: 2 on 1280") < order.index("SKIP quad")
    assert order.index("SKIP resolve") < order.index("quad          VS 871A3860C63CB28D "
                                                     "PS 2679C172F83DE85D")
    # The resolve's own mode-6 draw row is folded into its copy row.
    assert "pitch 320 mode 6" in order and order.count("pitch 320") == 2


def test_no_skips_is_an_error(tmp_path):
    p = tmp_path / "t.csv"
    rows = [r for r in frame_rows(0, 0) if "skip" not in r["kind"]]
    write(p, rows)
    _, total = pt.summarize(pt.read(p), None)
    assert total == 0
