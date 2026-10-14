import unittest
from pathlib import Path
from tools.codegen.direct_original_calls import safe_targets, rewrite, validate_backup_location


class DirectOriginalCalls(unittest.TestCase):
    source = '''DEFINE_REX_FUNC(sub_12345678) { ctx.r3.u64 += 7; }
DEFINE_REX_FUNC(sub_23456789) { ctx.r3.u64 = 17; }
DEFINE_REX_FUNC(sub_34567890) { ctx.f3.f64 *= 2; }
DEFINE_REX_FUNC(sub_45678901) { ctx.r3.u64 = 19; }
'''
    symbols = ''' 0001:00000000 sub_12345678 0000000140001000 f source.obj
 0001:00000000 __imp__sub_12345678 0000000140001000 f source.obj
 0001:00000010 sub_23456789 0000000140001010 f hook.obj
 0001:00000020 __imp__sub_23456789 0000000140001020 f source.obj
 0001:00000030 sub_34567890 0000000140001030 f source.obj
 0001:00000030 __imp__sub_34567890 0000000140001030 f source.obj
 0001:00000040 sub_45678901 0000000140001040 f source.obj
'''

    def test_hooks_float_bodies_and_unpaired_symbols_keep_public_calls(self):
        targets = safe_targets(self.symbols, self.source)
        self.assertEqual(targets, {'sub_12345678'})
        caller = 'sub_12345678(ctx, base); sub_23456789(ctx, base); sub_34567890(ctx, base); sub_45678901(ctx, base);'
        changed, counts = rewrite(caller, targets)
        self.assertEqual(changed, '__imp__' + caller)
        self.assertEqual(counts, {'sub_12345678': 1})

    def test_comments_strings_indirect_calls_and_function_mappings_are_untouched(self):
        caller = '''// sub_12345678(ctx, base);
/* sub_12345678(ctx, base); */
const char* text="sub_12345678(ctx, base);";
REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
{0x12345678, sub_12345678},
sub_12345678(ctx, base);
'''
        changed, counts = rewrite(caller, {'sub_12345678'})
        self.assertEqual(changed, caller.rsplit('sub_12345678(ctx, base);', 1)[0] + '__imp__sub_12345678(ctx, base);\n')
        self.assertEqual(counts, {'sub_12345678': 1})

    def test_float_or_unknown_callees_are_not_imported_through_integer_wrappers(self):
        source = self.source + '''DEFINE_REX_FUNC(sub_56789012) {sub_34567890(ctx, base);}
DEFINE_REX_FUNC(sub_67890123) {external(ctx, base);}
'''
        symbols = self.symbols
        for name in ('sub_56789012', 'sub_67890123'):
            for prefix in ('', '__imp__'):
                symbols += f' 0001:00000050 {prefix}{name} 0000000140001050 f source.obj\n'
        self.assertEqual(safe_targets(symbols, source), {'sub_12345678'})

    def test_backup_cannot_recurse_into_the_generated_tree(self):
        generated = Path.cwd() / 'generated-fixture'
        with self.assertRaises(ValueError):
            validate_backup_location(generated, generated / 'nested' / 'backup')
        validate_backup_location(generated, Path.cwd() / 'logs-fixture' / 'backup')

    def test_mmio_callbacks_are_not_treated_as_closed_integer_code(self):
        source = self.source + 'DEFINE_REX_FUNC(sub_78901234) {REX_MM_STORE_U32(ctx.r3.u32, 1);}\n'
        symbols = self.symbols
        for prefix in ('', '__imp__'):
            symbols += f' 0001:00000060 {prefix}sub_78901234 0000000140001060 f source.obj\n'
        self.assertEqual(safe_targets(symbols, source), {'sub_12345678'})


if __name__ == '__main__':
    unittest.main()
