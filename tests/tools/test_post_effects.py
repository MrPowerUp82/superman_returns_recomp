"""port/src/native_renderer/post_effects.h against the gameplay trace it was
derived from (docs/data/gpu_groups_gameplay_intel_uhd.csv)."""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "port" / "src" / "native_renderer" / "post_effects.h"
GROUPS = ROOT / "docs" / "data" / "gpu_groups_gameplay_intel_uhd.csv"
SCENE_SURFACE = 335545600


def table():
    text = HEADER.read_text(encoding="utf-8")
    surfaces = {name: int(value) for name, value in
                re.findall(r"constexpr uint32_t (kSurface\w+) = (\d+);", text)}
    body = text[text.index("kPasses[] = {"):]
    body = body[:body.index("};")]
    rows = re.findall(r"\{0x([0-9A-F]{16})ull, 0x([0-9A-F]{16})ull, (kSurface\w+)\}", body)
    return [(vs.lower(), ps.lower(), surfaces[s]) for vs, ps, s in rows]


def groups():
    with GROUPS.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def test_table_parses():
    assert len(table()) >= 10


def test_every_pass_is_a_traced_quad_on_a_small_surface():
    rows = groups()
    for vs, ps, surface in table():
        match = [r for r in rows if r["kind"] == "draw" and r["vs_hash"] == vs
                 and r["ps_hash"] == ps and int(r["rb_surface"]) == surface]
        assert len(match) == 1, (vs, ps, surface)
        r = match[0]
        assert r["primitive"] == "13" and r["rb_mode"] == "4"
        # RB_DEPTHCONTROL bits 0-2: stencil, depth test and depth write all off.
        assert int(r["rb_depth"]) & 0x7 == 0
        assert surface & 0x3FFF < 1280


def test_every_pass_has_one_resolve_per_draw():
    rows = groups()
    for vs, ps, surface in table():
        draws = sum(int(r["count"]) for r in rows if r["kind"] == "draw"
                    and r["vs_hash"] == vs and r["ps_hash"] == ps
                    and int(r["rb_surface"]) == surface)
        copies = sum(int(r["count"]) for r in rows if r["kind"] == "copy"
                     and r["ps_hash"] == ps and int(r["rb_surface"]) == surface)
        assert copies == draws, (ps, surface, draws, copies)


def test_no_listed_shader_draws_on_the_scene_surface():
    # The filter keys on (VS, PS, surface); make sure none of the listed pixel
    # shaders is also a 1280 pass that the table could be mistaken for.
    rows = groups()
    listed = {ps for _, ps, _ in table()}
    for r in rows:
        if r["kind"] == "draw" and r["ps_hash"] in listed:
            assert int(r["rb_surface"]) != SCENE_SURFACE, r
