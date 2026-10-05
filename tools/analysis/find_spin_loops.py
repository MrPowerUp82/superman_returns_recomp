#!/usr/bin/env python3
"""List busy-wait (spin) loop candidates in the recompiled code, to turn the
ones that burn CPU into real waits with mid-asm hooks (the same idea as the
"spin_idle_wait" hooks of rexglue-native-kit in Conan).

Runs on the owner's local port/generated/default/ (never committed). ReXGlue
v0.10.0 writes, for every guest instruction, a comment with its disassembly
("\\t// lwz r11,0(r31)") before the C++ it becomes, a label "loc_<ADDR>:" at
every branch target, and one DEFINE_REX_FUNC(<name>) per function; the
function addresses come from <project>_register.cpp (SetFunction(0x..., name))
or from a hexadecimal suffix of the name. db16cyc (the Xenon "wait 16 cycles"
hint) emits no C++ but keeps its comment, so it is visible here.

A loop is a branch back to an earlier address of the same function with at
most --max-len instructions in between. Kinds, strongest first:
  db16cyc   the loop contains db16cyc (the XDK spin hint), no call
  poll      loads, compares and branches only: no store, no call, and the
            load addresses do not change inside the loop (so it re-reads the
            same memory until another thread changes it)
  timebase  reads the time base (mftb) without loads: a timed delay
  poll+call like poll but calls a function (--calls); may already sleep
  atomic    lwarx/stwcx. retry loops, i.e. spin locks (--atomics)
Array scans (load addresses that move), copies (stores) and plain counting
loops are left out.

Static candidates only: which of them actually spin at run time needs a
profile (wpr / Very Sleepy with a -gcodeview build, or a hook that counts
iterations). Loops that already hold a mid-asm hook of the manifest are
marked "hooked" (e.g. the XMA wait at 0x826595B8).

usage:
  python tools/analysis/find_spin_loops.py [port/generated/default] [--csv logs/spin_loops.csv]
         [--max-len 16] [--calls] [--atomics] [--toml]
"""
from __future__ import annotations

import argparse
import csv
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DIR = ROOT / "port" / "generated" / "default"
MANIFEST = ROOT / "port" / "superman_returns_manifest.toml"

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
LABEL_RE = re.compile(r"^loc_([0-9A-Fa-f]+):")
INSN_RE = re.compile(r"^\t// (\S+)(?: (.*))?$")
REGISTER_RE = re.compile(r"SetFunction\((0x[0-9A-Fa-f]+),\s*(\w+)\)")
HEX_SUFFIX_RE = re.compile(r"_([0-9A-Fa-f]{8})$")
TARGET_RE = re.compile(r"0x([0-9a-fA-F]+)$")
GPR_RE = re.compile(r"\br(\d+)\b")

LOAD_RE = re.compile(r"^l(bz|hz|ha|wz|wa|d|fs|fd|wbr|hbr|dbr|vx|vxl|vlx|vrx|vebx|vehx|vewx)"
                     r"(u|x|ux)?$")
STORE_RE = re.compile(r"^st")
ATOMIC = {"lwarx", "ldarx", "stwcx.", "stdcx."}
CALLS = {"bl", "bla", "bctrl", "blrl", "bcctrl", "bclrl"}
SYNC = {"sync", "lwsync", "isync", "eieio", "ptesync"}
# Writes no GPR (or only a CR/SPR) even though its first operand may look like one.
NO_GPR_DEST = {"tw", "twi", "td", "tdi", "mtctr", "mtlr", "mtspr", "mtcrf", "mtxer", "dcbt",
               "dcbtst", "dcbf", "dcbz", "dcbz128", "dcbst", "icbi"}


@dataclass
class Insn:
    addr: int
    op: str
    args: str


@dataclass
class Function:
    name: str
    addr: int | None
    source: str
    insns: dict[int, Insn] = field(default_factory=dict)


@dataclass
class Loop:
    kind: str
    function: Function
    start: int
    branch: int
    body: list[Insn]
    loads: list[str]
    calls: list[str]
    hooked: bool = False

    @property
    def length(self) -> int:
        return len(self.body)


def function_addresses(directory: Path) -> dict[str, int]:
    found: dict[str, int] = {}
    for path in directory.glob("*_register.cpp"):
        for addr, name in REGISTER_RE.findall(path.read_text(encoding="utf-8", errors="replace")):
            found[name] = int(addr, 16)
    return found


def parse(directory: Path) -> list[Function]:
    addresses = function_addresses(directory)
    functions: list[Function] = []
    for path in sorted(directory.rglob("*.cpp")):
        if path.name.endswith("_register.cpp") or path.name.endswith("_init.cpp"):
            continue
        current: Function | None = None
        pc: int | None = None
        with path.open(encoding="utf-8", errors="replace") as f:
            for line in f:
                m = FUNC_RE.match(line)
                if m:
                    name = m.group(1)
                    addr = addresses.get(name)
                    if addr is None:
                        s = HEX_SUFFIX_RE.search(name)
                        addr = int(s.group(1), 16) if s else None
                    current = Function(name, addr, path.name)
                    functions.append(current)
                    pc = addr
                    continue
                if current is None:
                    continue
                m = LABEL_RE.match(line)
                if m:
                    pc = int(m.group(1), 16)
                    continue
                m = INSN_RE.match(line)
                if not m or m.group(1).endswith(":"):
                    continue  # ERROR:/FATAL:/UNIMPLEMENTED: notes, not instructions
                if pc is not None:
                    current.insns[pc] = Insn(pc, m.group(1), (m.group(2) or "").strip())
                    pc += 4
    return functions


def branch_target(insn: Insn) -> int | None:
    op = insn.op.rstrip("+-")
    if not op.startswith("b") or op in CALLS or op.startswith(("bclr", "bcctr", "blr", "bctr")):
        return None
    m = TARGET_RE.search(insn.args)
    return int(m.group(1), 16) if m else None


def split_args(args: str) -> list[str]:
    return [a.strip() for a in args.split(",") if a.strip()]


def load_base_registers(insn: Insn) -> set[str]:
    """GPRs that form the address of a load."""
    args = split_args(insn.args)
    if len(args) < 2:
        return set()
    if "(" in args[1]:
        inner = args[1][args[1].index("(") + 1:].rstrip(")")
        return {inner} if inner.startswith("r") else set()
    return {a for a in args[1:3] if a.startswith("r")}


def written_registers(insn: Insn) -> set[str]:
    op = insn.op.rstrip("+-")
    args = split_args(insn.args)
    if (not args or branch_target(insn) is not None or op in CALLS or STORE_RE.match(op)
            or op.startswith("cmp") or op in NO_GPR_DEST or op in SYNC):
        return set()
    out = set()
    if re.fullmatch(r"r\d+", args[0]):
        out.add(args[0])
    m = LOAD_RE.match(op)
    if m and m.group(2) and "u" in m.group(2):
        out |= load_base_registers(insn)  # update forms move the base
    return out


def classify(body: list[Insn], include_calls: bool, include_atomics: bool):
    ops = [i.op.rstrip("+-") for i in body]
    loads = [i for i in body if LOAD_RE.match(i.op)]
    calls = [f"{i.op} {i.args}".strip() for i in body if i.op in CALLS]
    if any(op in ATOMIC for op in ops):
        return ("atomic", loads, calls) if include_atomics else None
    if any(STORE_RE.match(op) for op in ops):
        return None
    written = set().union(*(written_registers(i) for i in body)) if body else set()
    bases = set().union(*(load_base_registers(i) for i in loads)) if loads else set()
    moving = bool(bases & written)
    if "db16cyc" in ops and not calls:
        return "db16cyc", loads, calls
    if calls:
        return ("poll+call", loads, calls) if include_calls and loads and not moving else None
    if loads and not moving:
        return "poll", loads, calls
    if not loads and "mftb" in ops:
        return "timebase", loads, calls
    return None


def manifest_hooks(path: Path) -> list[int]:
    if not path.exists():
        return []
    text = path.read_text(encoding="utf-8")
    return [int(a, 16) for a in
            re.findall(r"\[\[entrypoint\.midasm_hook\]\]\s*\naddress\s*=\s*(0x[0-9A-Fa-f]+)", text)]


def find_loops(functions: list[Function], max_len: int, include_calls: bool,
               include_atomics: bool, hooks: list[int]) -> list[Loop]:
    loops: list[Loop] = []
    for fn in functions:
        for addr, insn in fn.insns.items():
            target = branch_target(insn)
            if target is None or target > addr or (addr - target) // 4 + 1 > max_len:
                continue
            body = [fn.insns.get(a) for a in range(target, addr + 4, 4)]
            if any(i is None for i in body):
                continue  # not contiguous in this function
            result = classify(body, include_calls, include_atomics)
            if result is None:
                continue
            kind, loads, calls = result
            loop = Loop(kind, fn, target, addr, body,
                        [f"{i.op} {i.args}" for i in loads], calls)
            loop.hooked = any(target <= h <= addr for h in hooks)
            loops.append(loop)
    order = {"db16cyc": 0, "poll": 1, "timebase": 2, "poll+call": 3, "atomic": 4}
    loops.sort(key=lambda l: (order[l.kind], l.start))
    return loops


def write_csv(path: Path, loops: list[Loop]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["kind", "function", "function_addr", "loop_start", "branch_addr",
                    "instructions", "db16cyc", "loads", "calls", "hooked", "source", "disasm"])
        for l in loops:
            w.writerow([l.kind, l.function.name,
                        f"0x{l.function.addr:08X}" if l.function.addr is not None else "",
                        f"0x{l.start:08X}", f"0x{l.branch:08X}", l.length,
                        int(any(i.op == "db16cyc" for i in l.body)), " | ".join(l.loads),
                        " | ".join(l.calls), int(l.hooked), l.function.source,
                        " ; ".join(f"{i.op} {i.args}".strip() for i in l.body)])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("generated", type=Path, nargs="?", default=DEFAULT_DIR)
    parser.add_argument("--csv", type=Path, default=ROOT / "logs" / "spin_loops.csv")
    parser.add_argument("--max-len", type=int, default=16, help="instructions per loop (16)")
    parser.add_argument("--calls", action="store_true", help="also list poll loops with calls")
    parser.add_argument("--atomics", action="store_true", help="also list lwarx/stwcx. loops")
    parser.add_argument("--toml", action="store_true",
                        help="print commented [[entrypoint.midasm_hook]] stubs")
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    args = parser.parse_args()

    if not args.generated.is_dir():
        print(f"{args.generated} not found: run build.cmd (codegen) first", file=sys.stderr)
        return 2
    functions = parse(args.generated)
    if not functions:
        print(f"no DEFINE_REX_FUNC in {args.generated}", file=sys.stderr)
        return 2
    unknown = sum(1 for f in functions if f.addr is None)
    loops = find_loops(functions, args.max_len, args.calls, args.atomics,
                       manifest_hooks(args.manifest))
    write_csv(args.csv, loops)

    kinds: dict[str, int] = {}
    for l in loops:
        kinds[l.kind] = kinds.get(l.kind, 0) + 1
    print(f"{len(functions)} functions ({unknown} without a known address), "
          f"{len(loops)} candidate loops: "
          + ", ".join(f"{k} {n}" for k, n in kinds.items()))
    for l in loops:
        mark = " [hooked]" if l.hooked else ""
        print(f"{l.kind:9s} {l.function.name:24s} 0x{l.start:08X}-0x{l.branch:08X} "
              f"{l.length:2d} insns{mark}")
        for i in l.body:
            print(f"            {i.addr:08X}  {i.op} {i.args}".rstrip())
        if args.toml and not l.hooked:
            print(f"# [[entrypoint.midasm_hook]]   # {l.kind} loop in {l.function.name}")
            print(f"# address = 0x{l.branch:08X}   # the back branch; runs once per iteration")
            print('# name = "SrSpinIdleWait"      # e.g. yield/sleep after N iterations')
            print("# registers = [...]           # whatever the hook reads, e.g. the polled address")
    print(f"CSV: {args.csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
