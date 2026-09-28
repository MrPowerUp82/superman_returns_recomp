"""Find guest code that ReXGlue's static analysis never turned into functions.

Indirect calls (vtables, callbacks) into undiscovered code abort at runtime with
"Call to invalid or unregistered function". This script compares a dump of the
loaded guest image (see SR_DUMP_IMAGE in port/src/superman_returns_app.h) with
the generated sources and lists:

  * pointers stored in data sections that land in code no function covers
  * uncovered, non-zero regions of the code section (undecoded code)

Usage: python tools/find_missing_funcs.py logs/image.bin [--gaps] [--toml]
"""

import re
import struct
import sys
from bisect import bisect_right
from pathlib import Path

IMAGE_BASE = 0x82000000
ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "port" / "generated" / "default"

INSN_COMMENT = re.compile(r"^\t+// [a-z]")
ADDR_MARK = re.compile(r"^(?:loc_([0-9A-F]{8}):|\t+ctx\.lr = 0x([0-9A-F]{8});)")


def be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


def pe_sections(img):
    pe = struct.unpack_from("<I", img, 0x3C)[0]
    assert img[pe:pe + 4] == b"PE\0\0", "image dump does not start with a PE header"
    count = struct.unpack_from("<H", img, pe + 6)[0]
    opt_size = struct.unpack_from("<H", img, pe + 20)[0]
    table = pe + 24 + opt_size
    sections = []
    for i in range(count):
        s = table + i * 40
        name = img[s:s + 8].rstrip(b"\0").decode(errors="replace")
        vsize, vaddr = struct.unpack_from("<II", img, s + 8)
        flags = struct.unpack_from("<I", img, s + 36)[0]
        sections.append((name, IMAGE_BASE + vaddr, vsize, bool(flags & 0x20000000)))
    return sections


def load_functions():
    starts = {}
    for m in re.finditer(r"SetFunction\(0x([0-9A-F]{8}), (\w+)\)",
                         (GEN / "superman_returns_register.cpp").read_text()):
        starts[m.group(2)] = int(m.group(1), 16)

    # Size = instruction count, extended to the furthest label / return
    # address seen, since inline jump tables are not emitted as instructions.
    sizes = {}
    for cpp in GEN.glob("superman_returns_recomp.*.cpp"):
        name, count, last = None, 0, 0
        for line in cpp.read_text().splitlines():
            if line.startswith("DEFINE_REX_FUNC("):
                name, count, last = line[16:line.index(")")], 0, 0
            elif name and line == "}":
                sizes[name] = (count * 4, last)
                name = None
            elif name and INSN_COMMENT.match(line):
                count += 1
            elif name:
                m = ADDR_MARK.match(line)
                if m:
                    addr = int(m.group(1), 16) if m.group(1) else int(m.group(2), 16) - 4
                    last = max(last, addr)

    funcs = []
    for n, s in starts.items():
        size, last = sizes.get(n, (4, 0))
        funcs.append((s, max(s + size, last + 4), n))
    return sorted(funcs)


def describe(word):
    op = word >> 26
    if word == 0x7D8802A6:
        return "mflr r12 (prologue)"
    if op == 14 and (word >> 16) & 0x1F == 3 and (word >> 21) & 0x1F == 3:
        imm = word & 0xFFFF
        return f"addi r3,r3,{imm - 0x10000 if imm & 0x8000 else imm} (this-adjust thunk)"
    if op == 18:
        return "b ..."
    if word == 0x4E800020:
        return "blr"
    return f"{word:08X}"


def main():
    img = Path(sys.argv[1]).read_bytes()
    as_toml = "--toml" in sys.argv
    sections = pe_sections(img)
    code = [(va, va + size) for _, va, size, x in sections if x]
    data = [(va, va + size) for _, va, size, x in sections if not x]

    funcs = load_functions()
    start_set = {f[0] for f in funcs}
    starts = [f[0] for f in funcs]

    def in_code(addr):
        return any(lo <= addr < hi for lo, hi in code)

    def covered(addr):
        i = bisect_right(starts, addr) - 1
        return i >= 0 and funcs[i][0] <= addr < funcs[i][1]

    def word_at(addr):
        return be32(img, addr - IMAGE_BASE)

    # Pointers from data sections into uncovered code.
    targets = {}
    for lo, hi in data:
        for addr in range(lo, min(hi, IMAGE_BASE + len(img)) - 3, 4):
            v = word_at(addr)
            if v & 3 or v in start_set or not in_code(v) or covered(v):
                continue
            if word_at(v) == 0:
                continue
            targets.setdefault(v, []).append(addr)

    # Addresses materialized in code: lis rA,hi followed by addi/ori rD,rA,lo.
    for lo, hi in code:
        for addr in range(lo, hi - 3, 4):
            insn = word_at(addr)
            if insn >> 26 != 15 or (insn >> 16) & 0x1F != 0:  # lis == addis rD,0,imm
                continue
            reg, upper = (insn >> 21) & 0x1F, (insn & 0xFFFF) << 16
            for nxt in range(addr + 4, min(addr + 40, hi), 4):
                w = word_at(nxt)
                op, ra = w >> 26, (w >> 16) & 0x1F
                if ra == reg and op in (14, 24):  # addi / ori
                    imm = w & 0xFFFF
                    if op == 14 and imm & 0x8000:
                        imm -= 0x10000
                    v = (upper + imm) & 0xFFFFFFFF
                    if (not v & 3 and v not in start_set and in_code(v)
                            and not covered(v) and word_at(v) != 0):
                        targets.setdefault(v, []).append(addr)
                if (w >> 21) & 0x1F == reg and op not in (36, 38, 44, 37):
                    break  # reg overwritten (ignoring stores, which use rS)

    # A real entry point cannot be reached by falling through: the previous
    # word must be padding or an unconditional terminator (blr, bctr, b).
    # Switch jump tables sit inline after a bctr and hold code addresses.
    def entry_like(addr):
        prev = word_at(addr - 4)
        if in_code(word_at(addr)):
            return False
        return (prev == 0 or prev in (0x4E800020, 0x4E800420)
                or (prev >> 26 == 18 and not prev & 1))

    rejected = {t: r for t, r in targets.items() if not entry_like(t)}
    targets = {t: r for t, r in targets.items() if entry_like(t)}

    # Uncovered non-zero spans between known functions.
    gaps = []
    for (s0, e0, _), (s1, _, _) in zip(funcs, funcs[1:]):
        a = e0
        while a < s1 and word_at(a) == 0:
            a += 4
        if a < s1:
            gaps.append((a, s1))

    # Entry points inside uncovered gaps: code right after a terminator that is
    # not an inline jump table. Catches thunk tables indexed at runtime.
    gap_entries = {}
    if "--gaps" in sys.argv:
        for a, b in gaps:
            for p in range(a, b, 4):
                if p not in targets and word_at(p) != 0 and entry_like(p) and not covered(p):
                    gap_entries[p] = []
        targets.update(gap_entries)

    if as_toml:
        for t in sorted(targets):
            print(f"0x{t:08X} = {{ name = \"vt_{t:08X}\" }}")
        return

    print(f"sections: {[(n, hex(v), hex(s), x) for n, v, s, x in sections]}")
    print(f"functions: {len(funcs)}, uncovered non-zero gaps: {len(gaps)}")
    for title, group in (("entry points in uncovered code", targets),
                         ("rejected (falls through from previous insn)", rejected)):
        print(f"{title}: {len(group)}")
        for t in sorted(group):
            refs = ", ".join(f"{r:08X}" for r in group[t][:3])
            print(f"  {t:08X}  {describe(word_at(t)):<36} refs {refs}")
    print("largest gaps:")
    for a, b in sorted(gaps, key=lambda g: g[0] - g[1])[:15]:
        print(f"  {a:08X}-{b:08X} ({b - a} bytes) first={describe(word_at(a))}")


if __name__ == "__main__":
    main()
