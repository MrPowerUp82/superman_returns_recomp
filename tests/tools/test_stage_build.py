import hashlib
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('stage_build', Path(__file__).parents[2] / 'tools/release/stage_build.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def test_large_package_reassembles_exactly_with_integrity_metadata(tmp_path):
    source = tmp_path / 'build.zip'
    source.write_bytes(b'PK-package-with-launcher-' * 10)
    version = tmp_path / 'version.json'
    version.write_text(json.dumps({'version': 'test'}), encoding='utf-8-sig')
    out = tmp_path / 'out'
    info = module.stage_build(source, version, out, part_size=64)
    assert b''.join((out / p['name']).read_bytes() for p in info['zip_parts']) == source.read_bytes()
    assert info['zip_size'] == source.stat().st_size
    assert info['zip_sha256'] == hashlib.sha256(source.read_bytes()).hexdigest()
    for part in info['zip_parts']:
        assert part['size'] <= 64
        assert hashlib.sha256((out / part['name']).read_bytes()).hexdigest() == part['sha256']
    assert not (out / source.name).exists()


def test_small_package_keeps_legacy_zip_layout(tmp_path):
    source = tmp_path / 'build.zip'
    source.write_bytes(b'PK-small')
    version = tmp_path / 'version.json'
    version.write_text(json.dumps({'version': 'small', 'zip_parts': [], 'zip_size': 1, 'zip_sha256': 'old'}))
    out = tmp_path / 'out'
    info = module.stage_build(source, version, out, part_size=64)
    assert info == {'version': 'small'}
    assert (out / source.name).read_bytes() == source.read_bytes()
