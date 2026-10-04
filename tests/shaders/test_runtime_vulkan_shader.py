import sys,struct,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/shaders'))
from runtime_vulkan_shader import ready,failure
from unittest.mock import patch
class RuntimeTests(unittest.TestCase):
    def metadata(self):
        return dict(entrypoints=[{'model':0}],capabilities=[1],extensions=[],descriptors=[],inputs=[],outputs=[])
    def test_failure_keeps_stage_and_diagnostic(self):
        data=failure('ps','timeout')
        self.assertEqual(struct.unpack_from('<4I',data),(0x33525653,1,0,1))
        self.assertIn(b'timeout',data)
    def test_forbidden_abi_never_becomes_ready(self):
        m=self.metadata();m['capabilities']=[11]
        with patch('runtime_vulkan_shader.inspect_spirv',return_value=m):
            with self.assertRaisesRegex(ValueError,'int64'):ready('vs',b'bad')
    def test_stage_and_duplicate_locations_are_rejected(self):
        m=self.metadata()
        with patch('runtime_vulkan_shader.inspect_spirv',return_value=m):
            with self.assertRaisesRegex(ValueError,'stage'):ready('ps',b'bad')
        m['inputs']=[{'location':0,'type':'float32x4'}]*2
        with patch('runtime_vulkan_shader.inspect_spirv',return_value=m):
            with self.assertRaisesRegex(ValueError,'Duplicate'):ready('vs',b'bad')
    def test_hud_shader_with_duplicate_elements_compiles_ready(self):
        from compile_vulkan import compile_shader, Toolchain
        from srpaths import ROOT, TRANSLATOR, SHADER_COMMON, DXC
        hud = ROOT / 'artifacts/shaders/raw/978B0FF62C3693C2.vs.bin'
        if hud.is_file() and TRANSLATOR.is_file() and DXC.is_file():
            res = compile_shader(hud, 'vs', ROOT / 'build/vulkan-m2', Toolchain(TRANSLATOR, SHADER_COMMON, DXC))
            self.assertEqual(res.status, 'ready')
            self.assertEqual(res.diagnostic, '')
if __name__=='__main__':unittest.main()
