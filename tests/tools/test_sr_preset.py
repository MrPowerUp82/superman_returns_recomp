"""port/src/sr_preset.cpp: the Quality preset must be the compiled-in default
of every option it manages (docs/performance-design.md, decision 7), and every
managed option must exist."""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "port" / "src"


def defaults():
    found = {}
    for path in SRC.rglob("*.cpp"):
        text = path.read_text(encoding="utf-8")
        for kind, name, value in re.findall(
                r"REXCVAR_DEFINE_(BOOL|INT32|STRING)\((\w+),\s*([^,]+),", text):
            found[name] = value.strip().strip('"')
    return found


def preset_values():
    text = (SRC / "sr_preset.cpp").read_text(encoding="utf-8")
    body = text[text.index("kPresetValues[] = {"):]
    body = body[:body.index("};")]
    return re.findall(r'\{"(\w+)", "([^"]*)", "([^"]*)"\}', body)


def test_preset_table_parses():
    assert [name for name, _, _ in preset_values()] == ["sr_post_effects", "sr_render_scale"]


def test_quality_is_the_default():
    d = defaults()
    for name, quality, _ in preset_values():
        assert name in d, name
        assert d[name] == quality, (name, d[name], quality)


def test_render_scale_not_enabled_by_any_preset_yet():
    values = {name: (q, p) for name, q, p in preset_values()}
    assert values["sr_render_scale"] == ("100", "100")
    assert values["sr_post_effects"] == ("true", "false")
