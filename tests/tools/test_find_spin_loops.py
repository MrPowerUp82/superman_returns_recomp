"""tools/analysis/find_spin_loops.py on a synthetic generated directory shaped like
ReXGlue v0.10.0 output (DEFINE_REX_FUNC, loc_ labels, // disassembly)."""
import find_spin_loops as fsl

GENERATED = """\
DEFINE_REX_FUNC(sub_82100000) {
\tREX_FUNC_PROLOGUE();
\t// mflr r12
\tctx.r12.u64 = ctx.lr;
loc_82100004:
\t// lwz r11,0(r31)
\tctx.r11.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);
\t// cmpwi cr6,r11,0
\tctx.cr6.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);
\t// db16cyc
\t// beq cr6,0x82100004
\tif (ctx.cr6.eq) goto loc_82100004;
loc_82100014:
\t// lwz r10,4(r30)
\tctx.r10.u64 = PPC_LOAD_U32(ctx.r30.u32 + 4);
\t// clrlwi r10,r10,31
\tctx.r10.u64 = ctx.r10.u32 & 0x1;
\t// cmplwi cr6,r10,0
\tctx.cr6.compare<uint32_t>(ctx.r10.u32, 0, ctx.xer);
\t// bne cr6,0x82100014
\tif (!ctx.cr6.eq) goto loc_82100014;
loc_82100024:
\t// lbzu r9,1(r3)
\t// UNIMPLEMENTED: none
\t// cmpwi r9,0
\t// bne 0x82100024
loc_82100030:
\t// lwz r8,0(r4)
\t// stw r8,0(r5)
\t// addi r4,r4,4
\t// bdnz 0x82100030
\t// blr
}

DEFINE_REX_FUNC(vt_82200000) {
\tREX_FUNC_PROLOGUE();
loc_82200000:
\t// lwarx r11,0,r3
\t// cmpwi r11,0
\t// bne- 0x82200000
\t// stwcx. r4,0,r3
\t// bne- 0x82200000
loc_82200014:
\t// lwz r11,0(r31)
\t// bl 0x82300000
\t// cmpwi cr6,r11,0
\t// beq cr6,0x82200014
\t// blr
}

DEFINE_REX_FUNC(named_function) {
\tREX_FUNC_PROLOGUE();
\t// mftb r11
\t// subf r10,r3,r11
\t// cmplw cr6,r10,r4
\t// blt cr6,0x82400000
\t// blr
}
"""

REGISTER = "void p_RegisterFunctions(r) {\n  registrar->SetFunction(0x82400000, named_function);\n}\n"


def run(tmp_path, **kw):
    (tmp_path / "p_recomp.0.cpp").write_text(GENERATED)
    (tmp_path / "p_register.cpp").write_text(REGISTER)
    functions = fsl.parse(tmp_path)
    loops = fsl.find_loops(functions, kw.get("max_len", 16), kw.get("calls", False),
                           kw.get("atomics", False), kw.get("hooks", []))
    return functions, {(l.kind, l.start, l.branch) for l in loops}, loops


def test_addresses_follow_labels_and_register_file(tmp_path):
    functions, _, _ = run(tmp_path)
    by_name = {f.name: f for f in functions}
    assert by_name["sub_82100000"].insns[0x82100000].op == "mflr"
    assert by_name["sub_82100000"].insns[0x8210000C].op == "db16cyc"
    # The UNIMPLEMENTED note is not an instruction: addresses stay aligned.
    assert by_name["sub_82100000"].insns[0x8210002C].op == "bne"
    assert by_name["named_function"].addr == 0x82400000
    assert by_name["named_function"].insns[0x8240000C].op == "blt"


def test_default_kinds(tmp_path):
    _, found, _ = run(tmp_path)
    assert found == {
        ("db16cyc", 0x82100004, 0x82100010),
        ("poll", 0x82100014, 0x82100020),
        ("timebase", 0x82400000, 0x8240000C),
    }
    # Left out: the lbzu string scan, the copy loop, the spin lock, the call.


def test_optional_kinds(tmp_path):
    _, found, _ = run(tmp_path, calls=True, atomics=True)
    assert ("atomic", 0x82200000, 0x82200008) in found
    assert ("atomic", 0x82200000, 0x82200010) in found
    assert ("poll+call", 0x82200014, 0x82200020) in found


def test_max_len_and_hooks(tmp_path):
    # Each default loop has 4 instructions.
    assert run(tmp_path, max_len=3)[1] == set()
    assert len(run(tmp_path, max_len=4)[1]) == 3
    _, _, loops = run(tmp_path, hooks=[0x82100008])
    hooked = {l.start for l in loops if l.hooked}
    assert hooked == {0x82100004}


def test_manifest_hooks_parse():
    assert 0x826595B8 in fsl.manifest_hooks(fsl.MANIFEST)
    assert 0x822EB1C8 in fsl.manifest_hooks(fsl.MANIFEST)
