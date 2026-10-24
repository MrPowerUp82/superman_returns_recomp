import struct
import unittest
from pack_pipeline_seed import HEADER,pack_seed

class PipelineSeedTests(unittest.TestCase):
    def test_deduplicates_description_but_preserves_original_record_and_header_contract(self):
        a=struct.pack('<Q',11)+bytes(range(24))
        duplicate=struct.pack('<Q',22)+a[8:]
        different=struct.pack('<Q',33)+bytes(reversed(range(24)))
        seed,total,unique=pack_seed(HEADER.pack(b'CNPS',1,32,0)+a+duplicate+different)
        self.assertEqual((total,unique),(3,2))
        self.assertEqual(seed,HEADER.pack(b'CNPS',1,32,0)+a+different)
    def test_rejects_incompatible_and_partial_files(self):
        for data in [b'',HEADER.pack(b'NOPE',1,32,0),HEADER.pack(b'CNPS',2,32,0),
                     HEADER.pack(b'CNPS',1,31,0),HEADER.pack(b'CNPS',1,32,0)+b'x']:
            with self.subTest(data=data),self.assertRaises(ValueError):pack_seed(data)
    def test_different_fields_with_same_key_are_not_dropped(self):
        a=struct.pack('<Q',11)+bytes(24)
        b=a[:-1]+b'\x01'
        seed,total,unique=pack_seed(HEADER.pack(b'CNPS',1,32,0)+a+b)
        self.assertEqual((total,unique),(2,2));self.assertTrue(seed.endswith(a+b))

if __name__=='__main__':unittest.main()
