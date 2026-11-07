"""Structural regressions for the local EA APT menu transformation."""
import hashlib, json, re, tempfile, unittest
from pathlib import Path
import native_graphics_apt as menu

SOURCE = Path(__file__).resolve().parents[2] / 'game/DATA/ui.AST'

@unittest.skipUnless(SOURCE.exists(), 'requires the owner UI archive')
class NativeMenuAssets(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.source = SOURCE.read_bytes()
  cls.output = menu.transform(cls.source)
  cls.old, _ = menu.archive_records(cls.source)
  cls.new, _ = menu.archive_records(cls.output)

 def asset(self, name):
  package = next(r['data'] for r in self.new if r['name'] == name)
  records, _ = menu.archive_records(package)
  apt = next(r['data'] for r in records if r['data'].startswith(b'Apt Data:'))
  const = next(r['data'] for r in records if r['data'].startswith(b'Apt constant file'))
  return menu.inspect_pair(apt, const)

 def test_original_unchanged_and_unmodified_entries_exact(self):
  self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), menu.SOURCE_SHA)
  self.assertEqual(len(self.new), len(self.old) + 1)
  for before, after in zip(self.old, self.new):
   self.assertEqual(before['name'], after['name'])
   if before['name'] != 'SCRPAUSEMENU': self.assertEqual(before['data'], after['data'])
  template = next(r for r in self.old if r['name'] == 'SCRGAMEPLAYOPT')
  added = next(r for r in self.new if r['name'] == 'SCRGRAPHICSOPT')
  self.assertEqual(added['record'][:2], template['record'][:2])
  self.assertEqual(int.from_bytes(added['record'][2:10], 'little'), menu.fnv64('SCRGRAPHICSOPT'))

 def test_native_pause_has_ten_distinct_buttons(self):
  parsed = self.asset('SCRPAUSEMENU')
  sprite = next(c for c in parsed['movie']['characters'] if c['index'] == 2)
  placements = [c for c in sprite['frames'][0] if c['type'] == 3]
  self.assertEqual({c['instance_name'] for c in placements}, {'btn' + str(i) for i in range(1, 11)})
  self.assertEqual(len({c['depth'] for c in placements}), 10)
  self.assertIn('GRAPHICS OPTIONS', json.dumps(parsed))
  self.assertIn('screens/misc/ScrGraphicsOpt', json.dumps(parsed))

 def test_graphics_uses_original_toggles_and_tagged_bridge(self):
  parsed = self.asset('SCRGRAPHICSOPT')
  sprite = next(c for c in parsed['movie']['characters'] if c['index'] == 2)
  placements = [c for c in sprite['frames'][0] if c['type'] == 3]
  self.assertEqual({c['instance_name'] for c in placements}, {'srRow' + str(i) for i in range(6)})
  self.assertTrue(all(c['character'] == 1 for c in placements))
  self.assertEqual([x['name'] for x in parsed['movie']['exports']], ['ScrGraphicsOpt', 'ScrGraphicsOpt', '__Packages.BistroBaseScreen', '__Packages.ScrGraphicsOpt'])
  text = json.dumps(parsed)
  for token in ['ScrGraphicsOpt', 'GRAPHICS OPTIONS', 'SrRefresh', 'PauseGame', '21330', '21331', 'srPage']:
   self.assertIn(token, text)

 def test_register_push_uses_title_supported_pool_form(self):
  def check(rows):
   for row in rows:
    self.assertLessEqual(row['opcode'], 0xb8)
    if 'body' in row: check(row['body'])
  for name in ['SCRPAUSEMENU', 'SCRGRAPHICSOPT']:
   parsed = self.asset(name)
   for stream in parsed['streams']: check(stream['instructions'])

 def test_recipe_reconstructs_byte_exact(self):
  with tempfile.TemporaryDirectory() as folder:
   header = Path(folder) / 'recipe.h'
   menu.write_recipe(self.source, self.output, header)
   text = header.read_text()
   literals = bytes(map(int, re.search(r'kLiterals\[\]=\{(.*?)\};', text, re.S)[1].replace('\n', '').strip(',').split(',')))
   ops = re.findall(r'\{(\d+)u,(\d+)u,(\d+)u\}', text)
   rebuilt = bytearray()
   for at, size, offset in ops:
    at, size, offset = int(at), int(size), int(offset)
    rebuilt.extend(literals[offset:offset+size] if at == 0xffffffff else self.source[at:at+size])
   self.assertEqual(rebuilt, self.output)

 def test_toggle_arrows_keep_original_value_column_span(self):
  widths=[]
  def visit(rows):
   for i,row in enumerate(rows):
    if row.get('value')=='SetWidth' and i>=5:
     widths.append(rows[i-5].get('value'))
    if 'body' in row:visit(row['body'])
  for stream in self.asset('SCRGRAPHICSOPT')['streams']:visit(stream['instructions'])
  self.assertEqual(widths,[200]*6)

if __name__ == '__main__': unittest.main()
