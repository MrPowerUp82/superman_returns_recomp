"""make_zip.py <folder> <zip>: deflate every file of <folder> into <zip> (relative paths, forward slashes)."""
import os
import sys
import zipfile

src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for base, _, names in os.walk(src):
        for name in sorted(names):
            full = os.path.join(base, name)
            z.write(full, os.path.relpath(full, src).replace(os.sep, "/"))
