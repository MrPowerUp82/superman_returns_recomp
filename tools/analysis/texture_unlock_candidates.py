"""Report static evidence for SR's common resource unlock and texture wrappers.

Reads the owner's generated sources; emits metadata, never executable bytes.
These shapes and callers identify observation points, not complete writer coverage.
Usage: python tools/analysis/texture_unlock_candidates.py --out logs/texture_unlock_candidates.json
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGETS = {
    "sub_820F3C18": "common resource unlock (confirmed buffer tail target)",
    "sub_820F4000": "confirmed vertex buffer unlock",
    "sub_820F4150": "confirmed index buffer unlock",
    "sub_820FFCC8": "texture-shaped unlock wrapper",
    "sub_821002F8": "surface-shaped unlock wrapper through object+24",
    "sub_820FFCB0": "level-zero texture lock candidate",
    "sub_821002D8": "surface lock candidate through object+24",
    "sub_8235CA48": "three-plane update: lock three outputs, call producer, unlock and bind outputs",
    "sub_824707B0": "producer called with the three-plane output descriptor (codec identity unconfirmed)",
    "sub_8247E9D0": "sampled vtable+72 target; adapts output descriptor and tailcalls vtable+76",
    "sub_82480D80": "sampled vtable+76 target; planar frame copy or packed-output conversion",
}
EXPECTED = {
    "sub_824707B0": ["lwz r11,0(r3)", "lwz r11,72(r11)", "mtctr r11", "bctr "],
    "sub_8247E9D0": ["lwz r11,0(r3)", "mr r6,r4", "addi r5,r6,8", "lwz r4,4(r6)",
        "lwz r11,76(r11)", "mtctr r11", "bctr "],
    "sub_820FFCC8": ["lwz r11,48(r3)", "lwz r10,32(r3)",
        "rlwinm r5,r11,0,0,19", "rlwinm r4,r10,0,0,19", "b 0x820f3c18"],
    "sub_821002F8": ["lwz r3,24(r3)", "lwz r11,48(r3)", "lwz r10,32(r3)",
        "rlwinm r5,r11,0,0,19", "rlwinm r4,r10,0,0,19", "b 0x820f3c18"],
}


def collect(generated):
    result = {name: {"role": role, "callers": []} for name, role in TARGETS.items()}
    for path in sorted(generated.glob("superman_returns_recomp.*.cpp")):
        text = path.read_text(encoding="utf-8")
        for match in re.finditer(r"DEFINE_REX_FUNC\((\w+)\) \{(.*?)(?=\nDEFINE_REX_FUNC|\Z)", text, re.S):
            name, body = match.groups()
            if name in result:
                ops = re.findall(r"^\s*// (.*)$", body, re.M)
                result[name].update(source=path.name, instructions=ops,
                    instruction_comments_sha256=hashlib.sha256("\n".join(ops).encode()).hexdigest())
                if name in EXPECTED and ops != EXPECTED[name]:
                    raise ValueError(f"Wrapper shape changed: {name}; review before instrumenting")
            for target in result:
                if target + "(ctx, base)" not in body:
                    continue
                returns = re.findall(r"ctx\.lr = (0x[0-9A-F]+);\s*" + target + r"\(ctx, base\)", body)
                result[target]["callers"].append({"function": name, "source": path.name,
                    "return_addresses": returns, "tail_call": not returns})
    for name, value in result.items():
        if "instructions" not in value:
            raise ValueError(f"Missing generated entry: {name}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, default=ROOT / "port/generated/default")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--target", action="append", default=[], help="Additional observed target, e.g. 8247E9D0")
    args = parser.parse_args()
    for address in args.target:
        if not re.fullmatch(r"[0-9a-fA-F]{8}", address):
            parser.error("--target requires exactly eight hexadecimal digits")
        TARGETS.setdefault("sub_"+address.upper(), "additional runtime-observed target; semantics unconfirmed")
    report = collect(args.generated)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for name, value in report.items():
        print(f"{name}: {len(value['instructions'])} instructions, {len(value['callers'])} callers; {value['role']}")


if __name__ == "__main__":
    main()
