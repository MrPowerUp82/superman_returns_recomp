import sys,unittest,tempfile,json,struct,subprocess
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/shaders'))
from compile_vulkan import cache_key,compile_shader,Toolchain
from vulkan_contract import build_contract,compile_arguments
class CacheTests(unittest.TestCase):
 def test_cache_changes_for_backend_stage_tool_and_binding(self):
  c=build_contract();args=compile_arguments('vs')
  key=cache_key(b'a','vs','e','h','d',args,c)
  for stage,e,h,d,a,contract in [('ps','e','h','d',args,c),('vs','x','h','d',args,c),('vs','e','x','d',args,c),('vs','e','h','x',args,c),('vs','e','h','d',args+['-O0'],c),('vs','e','h','d',args,{**c,'version':2})]:
   self.assertNotEqual(key,cache_key(b'a',stage,e,h,d,a,contract))
 def setup_tools(self,root):
  paths=[]
  for name in ['emitter.exe','common.h','dxc.exe']:
   p=root/name;p.write_bytes(name.encode());paths.append(p)
  return Toolchain(*paths)
 def runner(self,args,**kwargs):
  if '-Fo' in args:Path(args[args.index('-Fo')+1]).write_bytes(struct.pack('<5I',0x07230203,0x10300,0,1,0))
  else:(Path(args[2])/(Path(args[3]).stem+'.hlsl')).write_text('void main(){}')
  return subprocess.CompletedProcess(args,0,'','')
 def test_corrupt_or_partial_artifact_is_not_ready(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);raw=root/'test.vs.bin';raw.write_bytes(b'container');tools=self.setup_tools(root)
   with patch('compile_vulkan.subprocess.run',side_effect=self.runner) as runner:
    first=compile_shader(raw,'vs',root,tools);self.assertEqual(first.status,'ready');self.assertEqual(runner.call_count,2)
    second=compile_shader(raw,'vs',root,tools);self.assertEqual(second.status,'ready');self.assertEqual(runner.call_count,2)
    first.binary_path.write_bytes(b'DXBC');compile_shader(raw,'vs',root,tools);self.assertEqual(runner.call_count,4)
    first.metadata_path.unlink();compile_shader(raw,'vs',root,tools);self.assertEqual(runner.call_count,6)
 def test_compiler_timeout_is_reported(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);raw=root/'test.ps.bin';raw.write_bytes(b'a');tools=self.setup_tools(root)
   with patch('compile_vulkan.subprocess.run',side_effect=subprocess.TimeoutExpired('emitter',60)):
    r=compile_shader(raw,'ps',root,tools);self.assertEqual(r.status,'failed');self.assertIn('timeout',r.diagnostic.lower())
 def test_unicode_paths_are_passed_as_arguments(self):
  with tempfile.TemporaryDirectory(prefix='Métrô espaço ') as tmp:
   root=Path(tmp);raw=root/'nome 日本.vs.bin';raw.write_bytes(b'a');tools=self.setup_tools(root)
   with patch('compile_vulkan.subprocess.run',side_effect=self.runner) as runner:
    self.assertEqual(compile_shader(raw,'vs',root,tools).status,'ready')
    self.assertTrue(all(isinstance(call.args[0],list) for call in runner.call_args_list))
class CatalogTests(unittest.TestCase):
 def test_catalog_attempts_spirv_independently_of_dxil_failure(self):
  import build_catalog
  from types import SimpleNamespace
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);dirs={k:root/k for k in ('raw','hlsl','dxil','spirv','logs')}
   for p in dirs.values():p.mkdir()
   # Invalid DXIL emitter still must cause a separate Vulkan attempt.
   args=SimpleNamespace(translator='emitter',include='common',dxc='dxc',spirv=True)
   with patch('build_catalog.run',return_value=(1,'','DXIL emitter failed')),patch('build_catalog.compile_shader',return_value=SimpleNamespace(status='failed',diagnostic='SPIR-V attempted',cache_key='',binary_path=None)) as compile:
    r=build_catalog.process({'container_hash':'ABC','type':'vs'},args,dirs)
    compile.assert_called_once();self.assertFalse(r['spirv']);self.assertIn('SPIR-V attempted',r['spirv_failure'])
if __name__=='__main__':unittest.main()
