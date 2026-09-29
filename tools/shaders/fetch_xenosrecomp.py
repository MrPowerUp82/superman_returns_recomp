#!/usr/bin/env python3
"""Fetch the shader translator sources into .tools/xenosrecomp (not committed).

  src/   zolaware/reblue-XenosRecomp at the kit's pin + xenosrecomp/patches/*.patch
  deps/  fmt and xxHash (header-only use) at pinned tags

Same pin and patch as crazyriddler/rexglue-native-kit @136bc6c4
(tools/xenosrecomp/fetch_source.sh); the kit vendored the patched tree, this
repository fetches it. Re-running replaces src/ after a pin or patch change.
usage: python tools/shaders/fetch_xenosrecomp.py [--force]
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from srpaths import XENOSRECOMP  # noqa: E402

HERE = Path(__file__).resolve().parent
XENOSRECOMP_URL = "https://github.com/zolaware/reblue-XenosRecomp"
XENOSRECOMP_PIN = "339af41df2c23dbe3256c1c377716b81a0e0fe6b"
DEPS = {
    # name: (url, tag) - the versions the kit SDK vendors.
    "fmt": ("https://github.com/fmtlib/fmt", "12.1.0"),
    "xxHash": ("https://github.com/Cyan4973/xxHash", "v0.8.3"),
}


def git(*args, cwd=None):
    subprocess.run(["git", *args], cwd=cwd, check=True)


def fetch_source(force):
    src = XENOSRECOMP / "src"
    stamp = src / ".sr_pin"
    patches = sorted((HERE / "xenosrecomp" / "patches").glob("*.patch"))
    want = XENOSRECOMP_PIN + "\n" + "\n".join(p.name for p in patches)
    if src.exists() and not force and stamp.exists() and stamp.read_text() == want:
        print(f"[xenosrecomp] source up to date at {src}")
        return
    if src.exists():
        shutil.rmtree(src)
    src.parent.mkdir(parents=True, exist_ok=True)
    git("clone", "-q", "--no-checkout", XENOSRECOMP_URL, str(src))
    git("-c", "advice.detachedHead=false", "checkout", "-q", XENOSRECOMP_PIN, cwd=src)
    for p in patches:
        print(f"[xenosrecomp] applying {p.name}")
        git("apply", "--whitespace=nowarn", str(p), cwd=src)
    stamp.write_text(want)
    print(f"[xenosrecomp] source at {src} ({XENOSRECOMP_PIN[:7]} + {len(patches)} patch(es))")


def fetch_deps():
    for name, (url, tag) in DEPS.items():
        dst = XENOSRECOMP / "deps" / name
        if dst.exists():
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        git("clone", "-q", "--depth", "1", "--branch", tag, url, str(dst))
        print(f"[xenosrecomp] {name} {tag} at {dst}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true", help="re-clone and re-patch the source")
    args = ap.parse_args()
    fetch_source(args.force)
    fetch_deps()


if __name__ == "__main__":
    main()
