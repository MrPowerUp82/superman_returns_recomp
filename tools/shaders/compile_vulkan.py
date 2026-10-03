"""Isolated Vulkan shader compilation; no translated game assets are distributed."""
from dataclasses import dataclass
from pathlib import Path
import hashlib,json,os,struct,subprocess,tempfile
from vulkan_contract import ABI,build_contract,compile_arguments

@dataclass(frozen=True)
class Toolchain:
 emitter:Path
 common:Path
 dxc:Path

@dataclass
class CompileResult:
 status:str
 cache_key:str=''
 binary_path:Path|None=None
 metadata_path:Path|None=None
 diagnostic:str=''

def digest(data):return hashlib.sha256(data).hexdigest()
def cache_key(container_bytes,stage,emitter_digest,common_digest,dxc_digest,arguments,contract):
 if stage not in ('vs','ps'):raise ValueError('Unsupported shader stage')
 identity={'schema':1,'backend':'vulkan','container':digest(container_bytes),'stage':stage,'emitter':emitter_digest,'common':common_digest,'dxc':dxc_digest,'arguments':arguments,'contract':contract}
 return digest(json.dumps(identity,sort_keys=True,separators=(',',':')).encode())
def tool_digest(path):
 path=Path(path);content=path.read_bytes()
 if path.name.lower()=='dxc.exe':
  for name in ('dxcompiler.dll','dxil.dll'):
   dll=path.parent/name
   if dll.exists():content+=name.encode()+dll.read_bytes()
 return digest(content)
def valid_spirv(data):
 if len(data)<20 or len(data)%4:return False
 magic,version,_,bound,reserved=struct.unpack_from('<5I',data)
 return magic==0x07230203 and 0x10000<=version<=0x10300 and bound>0 and reserved==0

def compile_shader(container,stage,output_root,tools):
 key=''
 try:
  container=Path(container);raw=container.read_bytes();args=compile_arguments(stage);contract=build_contract()
  fingerprints={'emitter':tool_digest(tools.emitter),'common':tool_digest(tools.common),'dxc':tool_digest(tools.dxc)}
  key=cache_key(raw,stage,fingerprints['emitter'],fingerprints['common'],fingerprints['dxc'],args,contract)
  directory=Path(output_root)/'shader_cache'/'vulkan'/key;binary=directory/'shader.spv';metadata=directory/'artifact.json'
  try:
   data=binary.read_bytes();m=json.loads(metadata.read_text(encoding='utf-8'))
   if m.get('schema')==1 and m.get('backend')=='vulkan' and m.get('abi')==ABI and m.get('stage')==stage and m.get('cache_key')==key and m.get('binary_sha256')==digest(data) and valid_spirv(data):
    return CompileResult('ready',key,binary,metadata)
  except (OSError,ValueError,TypeError):pass
  directory.mkdir(parents=True,exist_ok=True)
  with tempfile.TemporaryDirectory(prefix='compile-',dir=directory) as temp:
   work=Path(temp);source=work/('shader.'+stage+'.bin');source.write_bytes(raw)
   # No shell quoting: both Unicode and spaces remain one argument.
   try:
    result=subprocess.run([str(Path(tools.emitter).resolve()),str(Path(tools.common).resolve()),str(work.resolve()),str(source.resolve())],capture_output=True,text=True,errors='replace',timeout=60)
   except subprocess.TimeoutExpired:return CompileResult('failed',key,diagnostic='emitter timeout (60 seconds)')
   hlsl=work/('shader.'+stage+'.hlsl')
   if result.returncode or not hlsl.exists():return CompileResult('failed',key,diagnostic='emitter: '+(result.stderr+result.stdout)[:8192])
   compiled=work/'shader.spv'
   try:
    result=subprocess.run([str(Path(tools.dxc).resolve()),*args,'-Qstrip_debug',str(hlsl.resolve()),'-Fo',str(compiled.resolve())],capture_output=True,text=True,errors='replace',timeout=120)
   except subprocess.TimeoutExpired:return CompileResult('failed',key,diagnostic='DXC timeout (120 seconds)')
   if result.returncode or not compiled.exists():return CompileResult('failed',key,diagnostic='DXC: '+(result.stderr+result.stdout)[:8192])
   data=compiled.read_bytes()
   if not valid_spirv(data):return CompileResult('failed',key,diagnostic='DXC output is not Vulkan 1.1 SPIR-V')
   m={'schema':1,'backend':'vulkan','abi':ABI,'stage':stage,'cache_key':key,'fingerprints':fingerprints,'arguments':args,'contract':contract,'container_sha256':digest(raw),'binary_sha256':digest(data)}
   pending=work/'artifact.json';pending.write_text(json.dumps(m,sort_keys=True,indent=2),encoding='utf-8')
   os.replace(compiled,binary);os.replace(pending,metadata)
  return CompileResult('ready',key,binary,metadata)
 except (OSError,ValueError,TypeError) as e:return CompileResult('failed',key,diagnostic=str(e))
