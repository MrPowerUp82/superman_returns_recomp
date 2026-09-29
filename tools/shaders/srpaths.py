"""Repository paths shared by the shader pipeline scripts (replaces the kit's
tools/kitcfg.py + kit.env). Everything generated goes to artifacts/ or
.tools/, both ignored by Git: never commit shader outputs or game data."""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / "game"
GAME_NAME = "superman_returns"
TITLE_ID = "454107ED"
ARTIFACTS = ROOT / "artifacts" / "shaders"
TOOLS = ROOT / ".tools"
XENOSRECOMP = TOOLS / "xenosrecomp"
EXE = ".exe" if sys.platform == "win32" else ""
TRANSLATOR = XENOSRECOMP / "build" / f"XenosRecompCorpus{EXE}"
SHADER_COMMON = XENOSRECOMP / "src" / "XenosRecomp" / "shader_common.h"
DXC = TOOLS / "dxc" / "bin" / "x64" / f"dxc{EXE}"
NATIVE_SRC = ROOT / "port" / "src" / "native_renderer"


def default_image():
    """Decoded executable image (0x82000000..), written by the game with
    SR_DUMP_IMAGE=<path> (tools/native_validate.ps1 -Step image)."""
    for p in (ROOT / "port" / "logs" / "default_image.bin", ROOT / "logs" / "default_image.bin"):
        if p.exists():
            return p
    return None


def env_path(name, default):
    value = os.environ.get(name)
    return Path(value) if value else default
