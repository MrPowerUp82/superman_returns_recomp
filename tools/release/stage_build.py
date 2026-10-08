"""Stage the release ZIP for raw.github downloads, splitting oversized Git blobs."""
import hashlib
import json
import shutil
import sys
from pathlib import Path


def stage_build(zip_path, version_path, destination, part_size=80 * 1024 * 1024):
    zip_path, version_path, destination = map(Path, (zip_path, version_path, destination))
    destination.mkdir(parents=True, exist_ok=True)
    info = json.loads(version_path.read_text(encoding='utf-8-sig'))
    size = zip_path.stat().st_size
    hasher = hashlib.sha256()
    with zip_path.open('rb') as source:
        for data in iter(lambda: source.read(1024 * 1024), b''):
            hasher.update(data)
    digest = hasher.hexdigest()
    for field in ('zip_parts', 'zip_size', 'zip_sha256'):
        info.pop(field, None)
    if size > part_size:
        parts = []
        with zip_path.open('rb') as source:
            while data := source.read(part_size):
                name = f'{zip_path.stem}.{digest[:16]}.part{len(parts) + 1:03d}'
                (destination / name).write_bytes(data)
                parts.append({'name': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        info.update(zip_parts=parts, zip_size=size, zip_sha256=digest)
    else:
        shutil.copy2(zip_path, destination / zip_path.name)
    (destination / (zip_path.name + '.sha256')).write_text(f'{digest}  {zip_path.name}\n', encoding='ascii')
    (destination / 'version.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    return info


if __name__ == '__main__':
    stage_build(*sys.argv[1:])
