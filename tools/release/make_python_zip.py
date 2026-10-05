"""Zip a Python standard library (Lib/) for the embeddable layout used by the
Vulkan runtime shader scripts in the release package.

  python make_python_zip.py <Lib directory> <output .zip>

Only .py sources go in; packages a shader worker never needs are left out.
"""
import sys
import zipfile
from pathlib import Path

SKIP = {'site-packages', 'test', 'tests', 'idlelib', 'tkinter', 'turtledemo', 'ensurepip',
        'venv', '__pycache__', 'lib2to3', 'pydoc_data', 'sqlite3', 'ctypes', '__phello__'}


def main(lib, out):
    lib, out = Path(lib), Path(out)
    count = 0
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(lib.rglob('*.py')):
            relative = path.relative_to(lib)
            if SKIP.intersection(relative.parts[:-1]):
                continue
            archive.write(path, relative.as_posix())
            count += 1
    print(f'{count} modules -> {out} ({out.stat().st_size // 1024} KB)')
    return 0 if count else 1


if __name__ == '__main__':
    raise SystemExit(main(*sys.argv[1:3]))
