import struct,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/shaders'))
from spirv_metadata import inspect_spirv,validate_pair,contract_errors
from validate_vulkan_corpus import requirements

def module(*instructions):
 words=[0x07230203,0x10300,0,100,0]
 for opcode,args in instructions:words += [(len(args)+1)<<16|opcode,*args]
 return struct.pack('<'+str(len(words))+'I',*words)
class MetadataTests(unittest.TestCase):
 def test_malformed_instruction_is_rejected(self):
  with self.assertRaises(ValueError):inspect_spirv(module()+struct.pack('<I',0))
  with self.assertRaises(ValueError):inspect_spirv(module()+struct.pack('<I',6<<16|21))
 def test_runtime_array_and_physical_addresses_violate_contract(self):
  m=inspect_spirv(module((17,[1]),(17,[5347]),(25,[1,2,1,0,0,0,1,0]),(29,[3,1]),(32,[4,0,3]),(59,[4,5,0]),(71,[5,34,1]),(71,[5,33,0])))
  errors=contract_errors(m);self.assertTrue(any('physical' in e.lower() for e in errors));self.assertTrue(any('runtime descriptor' in e.lower() for e in errors))
 def test_pair_location_type_mismatch_is_reported(self):
  vs={'outputs':[dict(location=0,type='float32x4')],'inputs':[],'descriptors':[]}
  ps={'outputs':[],'inputs':[dict(location=0,type='uint32x4')],'descriptors':[]}
  self.assertTrue(validate_pair(vs,ps))
 def test_byte_buffer_runtime_member_is_not_descriptor_array(self):
  m=inspect_spirv(module((17,[1]),(21,[1,32,0]),(29,[2,1]),(30,[3,2]),(32,[4,12,3]),(59,[4,5,12]),(71,[5,34,0]),(71,[5,33,0])))
  self.assertFalse(contract_errors(m));self.assertEqual(m['descriptors'][0]['count'],1)
 def test_dynamic_descriptor_indexing_is_detected_without_capability(self):
  m=inspect_spirv(module((17,[1]),(21,[1,32,0]),(43,[1,2,32]),(25,[3,1,1,0,0,0,1,0]),(28,[4,3,2]),(32,[5,0,4]),(59,[5,6,0]),(71,[6,34,1]),(71,[6,33,0]),(32,[10,1,1]),(59,[10,11,1]),(61,[1,9,11]),(65,[5,8,6,9])))
  self.assertTrue(m['descriptors'][0]['dynamic_indexing'])
 def test_type_without_id_is_rejected(self):
  with self.assertRaises(ValueError):inspect_spirv(module((19,[])))
 def test_truncated_specialization_constant_is_rejected(self):
  with self.assertRaises(ValueError):inspect_spirv(module((50,[1])))
 def test_interface_and_sampler_requirements(self):
  m={'descriptors':[dict(type='sampler',count=32,set=2,dynamic_indexing=True)],'capabilities':[],
     'inputs':[dict(location=0,type='float32x3')],
     'outputs':[dict(location=i,type='float32x4') for i in range(20)]}
  self.assertEqual(requirements(m,'vs')['vertex_output_components'],80)
  self.assertEqual(requirements(m,'ps')['fragment_input_components'],3)
  self.assertTrue(requirements(m,'ps')['sampled_image_dynamic_indexing'])
if __name__=='__main__':unittest.main()
