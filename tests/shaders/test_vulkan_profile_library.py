import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/shaders'))
from make_vulkan_preshaders import serialize, main
from runtime_vulkan_shader import ready
from verify_vulkan_preshaders import verify


class ProfileLibraryTests(unittest.TestCase):
    def setUp(self):
        self.container=struct.pack('>3I',0x102A1101,12,12)+bytes(12)
        words=(0x07230203,0x10300,0,2,0,2<<16|17,1,5<<16|15,0,1,0x6e69616d,0)
        self.binary=struct.pack('<'+'I'*len(words),*words)

    def test_global_pack_verifies_only_with_global_profile(self):
        wire=ready('vs',self.binary,profile='global')
        library=serialize([(0,self.container,wire)],profile='global')
        self.assertEqual(struct.unpack_from('<I',library,8),(2,))
        self.assertEqual(verify(library,profile='global'),1)
        with self.assertRaises(ValueError):verify(library)
        portable=serialize([(0,self.container,ready('vs',self.binary))])
        self.assertEqual(verify(portable),1)
        with self.assertRaises(ValueError):verify(portable,profile='global')

    def test_pack_rejects_wire_from_other_profile(self):
        for pack,wire in [('portable','global'),('global','portable')]:
            with self.assertRaisesRegex(ValueError,'ABI'):
                serialize([(0,self.container,ready('vs',self.binary,profile=wire))],profile=pack)

    def test_cli_global_compiles_and_uses_distinct_default_filename(self):
        for profile,filename in [('portable','superman_returns_vulkan.srvk'),('global','superman_returns_vulkan_global.srvk')]:
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);corpus=root/'corpus';corpus.mkdir()
                (corpus/'example.vs.bin').write_bytes(self.container)
                compiled=root/'shader.spv';compiled.write_bytes(self.binary)
                def compile_for_profile(*args,**kwargs):
                    self.assertEqual(kwargs.get('profile'),profile)
                    return SimpleNamespace(status='ready',binary_path=compiled)
                argv=['make_vulkan_preshaders.py','--corpus',str(corpus)]
                if profile=='global':argv+=['--profile','global']
                with patch.object(sys,'argv',argv),patch('make_vulkan_preshaders.ROOT',root),patch('make_vulkan_preshaders.compile_shader',side_effect=compile_for_profile),contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(main(),0)
                output=root/'artifacts/shaders'/filename
                self.assertEqual(verify(output.read_bytes(),profile=profile),1)
                report=json.loads(output.with_suffix('.report.json').read_text())
                self.assertEqual(report['abi'],'sr-vulkan-global-v1' if profile=='global' else 'sr-vulkan-buffers-v1')


if __name__=='__main__':unittest.main()
