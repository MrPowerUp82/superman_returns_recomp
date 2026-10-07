"""Reject incomplete distribution ZIPs before they can reach GitHub."""
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
REQUIRED = (
    "superman_returns.exe", "SupermanReturnsLauncher.exe",
    "rexruntime.dll", "rexgpu-xenos.dll",
    "msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll",
    "licenses/DOTNET-LICENSE.txt", "licenses/DOTNET-ThirdPartyNotices.txt",
    "shader_tools/sr_xenosrecomp.exe", "shader_tools/dxc.exe",
    "shader_tools/dxcompiler.dll", "shader_tools/dxil.dll", "shader_tools/shader_common.h",
    "shader_tools/vulkan/sr_xenosrecomp.exe", "shader_tools/vulkan/shader_common.h",
    "shader_tools/vulkan/runtime_vulkan_shader.py", "shader_tools/vulkan/compile_vulkan.py",
    "shader_tools/vulkan/spirv_metadata.py", "shader_tools/vulkan/validate_vulkan_corpus.py",
    "shader_tools/vulkan/vulkan_contract.py", "shader_tools/python/python.exe",
    "run.cmd", "run_keyboard.cmd", "LEIAME.txt", "THIRD_PARTY_NOTICES.txt", "version.json",
)


def verify(tmp_path, missing=None, extra=None, empty=None):
    archive = tmp_path / "release.zip"
    with zipfile.ZipFile(archive, "w") as package:
        for name in REQUIRED:
            if name != missing:
                package.writestr(name, b"" if name == empty else b"fixture")
        if extra:
            package.writestr(extra, b"game-derived")
    return subprocess.run(
        [sys.executable, str(ROOT / "tools/release/verify_package.py"), str(archive)],
        capture_output=True, text=True,
    )


def test_complete_package_is_accepted(tmp_path):
    result = verify(tmp_path)
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize("missing", [
    "SupermanReturnsLauncher.exe", "licenses/DOTNET-ThirdPartyNotices.txt",
    "shader_tools/vulkan/sr_xenosrecomp.exe", "shader_tools/python/python.exe",
])
def test_incomplete_package_is_rejected(tmp_path, missing):
    result = verify(tmp_path, missing=missing)
    assert result.returncode != 0
    assert missing in result.stderr


def test_empty_launcher_is_rejected(tmp_path):
    result = verify(tmp_path, empty="SupermanReturnsLauncher.exe")
    assert result.returncode != 0
    assert "SupermanReturnsLauncher.exe" in result.stderr


@pytest.mark.parametrize("extra", [
    "superman_returns_shaders.srsl", "superman_returns_shaders.pak",
    "superman_returns_vulkan.srvk", "game/default.xex",
])
def test_local_game_data_is_rejected(tmp_path, extra):
    result = verify(tmp_path, extra=extra)
    assert result.returncode != 0
    assert extra in result.stderr
