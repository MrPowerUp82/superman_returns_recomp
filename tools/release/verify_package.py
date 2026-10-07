"""Check the distribution ZIP before packaging succeeds or GitHub is updated."""
import argparse
import zipfile
from pathlib import PurePosixPath

REQUIRED = {
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
}
LOCAL_LIBRARIES = {
    "superman_returns_shaders.srsl", "superman_returns_shaders.pak",
    "superman_returns_vulkan.srvk",
}


def verify_package(path):
    with zipfile.ZipFile(path) as package:
        files = {entry.filename: entry for entry in package.infolist() if not entry.is_dir()}
        missing = sorted(name for name in REQUIRED if name not in files or not files[name].file_size)
        if missing:
            raise ValueError("Missing or empty release files: " + ", ".join(missing))
        local = sorted(name for name in files
                       if PurePosixPath(name).name.lower() in LOCAL_LIBRARIES
                       or name.lower().startswith("game/"))
        if local:
            raise ValueError("Local game data in release: " + ", ".join(local))
        corrupt = package.testzip()
        if corrupt:
            raise ValueError("Corrupt ZIP entry: " + corrupt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zip")
    args = parser.parse_args()
    try:
        verify_package(args.zip)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Release package validation failed: {error}\n")
    print("Release ZIP OK: launcher, runtime and Vulkan shader tools included.")


if __name__ == "__main__":
    main()
