import sys,struct,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/shaders'))
from runtime_vulkan_shader import ready,failure,main,FIELDS
from types import SimpleNamespace
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
    def test_global_wire_marks_abi_and_reflects_interface_without_fixed_heap_counts(self):
        m=self.metadata();m['capabilities']=[1,29,32,33,5301,5302,5307,5308]
        m['extensions']=['SPV_EXT_descriptor_indexing']
        m['descriptors']=[dict(set=0,binding=0,type='sampled_image_2d',count=0,runtime_array=True,dynamic_indexing=True)]
        m['inputs']=[dict(location=2,type='float32x4')]
        m['outputs']=[dict(location=3,type='float32x2')]
        with patch('runtime_vulkan_shader.inspect_spirv',return_value=m):
            with self.assertRaisesRegex(ValueError,'portable ABI'):ready('vs',b'bad')
            data=ready('vs',b'global',profile='global')
        self.assertEqual(struct.unpack_from('<4I',data),(0x33525653,2,1,0))
        q=dict(zip(FIELDS,struct.unpack_from('<12I',data,16)))
        self.assertEqual([q[k] for k in FIELDS[:4]],[0,0,0,0])
        self.assertEqual([q[k] for k in FIELDS[4:]],[1,3,14,0,0,1,1,1])
        m['entrypoints']=[{'model':4}]
        with patch('runtime_vulkan_shader.inspect_spirv',return_value=m):data=ready('ps',b'global',profile='global')
        self.assertEqual(struct.unpack_from('<12I',data,16),(0,0,0,0,1,0,0,12,0,1,1,1))
        self.assertEqual(struct.unpack_from('<4I',failure('ps','failure',profile='global')),(0x33525653,2,0,1))
    def test_global_contract_and_unknown_profiles_fail_closed(self):
        m=self.metadata();m['descriptors']=[dict(set=1,binding=0,type='sampled_image_2d',count=32,runtime_array=False)]
        with patch('runtime_vulkan_shader.inspect_spirv',return_value=m):
            with self.assertRaisesRegex(ValueError,'Global descriptor ABI mismatch'):ready('vs',b'bad',profile='global')
        for function,args in [(ready,('vs',b'bad')),(failure,('vs','bad'))]:
            with self.assertRaisesRegex(ValueError,'profile'):function(*args,profile='unknown')
    def test_cli_forwards_profile_and_writes_matching_failure_abi(self):
        for profile,abi in [('portable',1),('global',2)]:
            with tempfile.TemporaryDirectory() as tmp:
                output=Path(tmp)/'result.bin'
                argv=['runtime_vulkan_shader.py','--container','raw.bin','--cache',tmp,'--emitter','emitter','--common','common','--dxc','dxc','--stage','vs','--result',str(output)]
                if profile=='global':argv+=['--profile','global']
                def compile_for_profile(*args,**kwargs):
                    self.assertEqual(kwargs.get('profile'),profile)
                    return SimpleNamespace(status='failed',diagnostic='controlled translation failure')
                with patch.object(sys,'argv',argv),patch('runtime_vulkan_shader.compile_shader',side_effect=compile_for_profile):
                    self.assertEqual(main(),0)
                self.assertEqual(struct.unpack_from('<4I',output.read_bytes()),(0x33525653,abi,0,0))
    def test_cli_global_ready_and_exception_preserve_profile(self):
        words=(0x07230203,0x10300,0,2,0,2<<16|17,1,5<<16|15,0,1,0x6e69616d,0)
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/'shader.spv';binary.write_bytes(struct.pack('<'+'I'*len(words),*words))
            output=Path(tmp)/'result.bin'
            argv=['runtime_vulkan_shader.py','--container','raw.bin','--cache',tmp,'--emitter','emitter','--common','common','--dxc','dxc','--stage','vs','--profile','global','--result',str(output)]
            for compiled,status in [(SimpleNamespace(status='ready',binary_path=binary),1),(ValueError('translation exception'),0)]:
                with patch.object(sys,'argv',argv),patch('runtime_vulkan_shader.compile_shader',side_effect=[compiled]):
                    self.assertEqual(main(),0)
                self.assertEqual(struct.unpack_from('<4I',output.read_bytes()),(0x33525653,2,status,0))
    def test_hud_shader_with_duplicate_elements_compiles_ready(self):
        from compile_vulkan import compile_shader, Toolchain
        from srpaths import ROOT, TRANSLATOR, SHADER_COMMON, DXC
        hud = ROOT / 'artifacts/shaders/raw/978B0FF62C3693C2.vs.bin'
        if hud.is_file() and TRANSLATOR.is_file() and DXC.is_file():
            res = compile_shader(hud, 'vs', ROOT / 'build/vulkan-m2', Toolchain(TRANSLATOR, SHADER_COMMON, DXC))
            self.assertEqual(res.status, 'ready')
            self.assertEqual(res.diagnostic, '')
if __name__=='__main__':unittest.main()
