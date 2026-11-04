"""Inspect the owner's PPC APT assets without executing or rewriting them.

Supports the big-endian, 32-bit, decoupled 1:7:4 variant observed in this game.
Format reference: https://github.com/LibreSAGE/AptTools (7b6d499340181d15e484b58c0fc78b14215a5455).
This is structural disassembly, not an ActionScript interpreter/decompiler.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


SIMPLE = {
    0x00: 'End', 0x04: 'NextFrame', 0x05: 'PreviousFrame', 0x06: 'Play',
    0x07: 'Stop', 0x08: 'ToggleQuality', 0x09: 'StopSounds', 0x0A: 'Add',
    0x0B: 'Subtract', 0x0C: 'Multiply', 0x0D: 'Divide', 0x0E: 'Equals',
    0x0F: 'LessThan', 0x10: 'And', 0x11: 'Or', 0x12: 'Not',
    0x13: 'StringEquals', 0x14: 'StringLength', 0x15: 'Substring',
    0x17: 'Pop', 0x18: 'ToInteger', 0x1C: 'GetVariable', 0x1D: 'SetVariable',
    0x20: 'SetTarget2', 0x21: 'StringAdd', 0x22: 'GetProperty',
    0x23: 'SetProperty', 0x24: 'CloneSprite', 0x25: 'RemoveSprite',
    0x26: 'Trace', 0x27: 'StartDrag', 0x28: 'StopDrag',
    0x29: 'StringLessThan', 0x2A: 'Throw', 0x2B: 'Cast', 0x2C: 'Implements',
    0x30: 'Random', 0x31: 'MBLength', 0x32: 'CharToAscii', 0x33: 'AsciiToChar',
    0x34: 'GetTimer', 0x35: 'MBSubstring', 0x36: 'MBCharToAscii',
    0x37: 'MBAsciiToChar', 0x3A: 'Delete', 0x3B: 'Delete2',
    0x3C: 'DefineLocal', 0x3D: 'CallFunction', 0x3E: 'Return',
    0x3F: 'Modulo', 0x40: 'NewObject', 0x41: 'DefineLocal2',
    0x42: 'InitArray', 0x43: 'InitObject', 0x44: 'TypeOf',
    0x45: 'TargetPath', 0x46: 'Enumerate', 0x47: 'Add2',
    0x48: 'LessThan2', 0x49: 'Equals2', 0x4A: 'ToNumber', 0x4B: 'ToString',
    0x4C: 'Duplicate', 0x4D: 'Swap', 0x4E: 'GetMember', 0x4F: 'SetMember',
    0x50: 'Increment', 0x51: 'Decrement', 0x52: 'CallMethod',
    0x53: 'NewMethod', 0x54: 'InstanceOf', 0x55: 'Enumerate2',
    0x56: 'PushThis', 0x58: 'PushGlobal', 0x59: 'PushZero', 0x5A: 'PushOne',
    0x5B: 'CallFunctionPop', 0x5C: 'CallFunctionSetVariable',
    0x5D: 'CallMethodPop', 0x5E: 'CallMethodSetVariable',
    0x60: 'BitAnd', 0x61: 'BitOr', 0x62: 'BitXor', 0x63: 'ShiftLeft',
    0x64: 'ShiftRight', 0x65: 'ShiftUnsigned', 0x66: 'StrictEquals',
    0x67: 'Greater', 0x69: 'Extends', 0x70: 'PushThisVariable',
    0x71: 'PushGlobalVariable', 0x72: 'PushZeroSetVariable',
    0x73: 'PushTrue', 0x74: 'PushFalse', 0x75: 'PushNull',
    0x76: 'PushUndefined', 0xBA: 'Breakpoint',
}
DICT = {0xA2: 'DictionaryPush', 0xA3: 'DictionaryPushWord',
        0xAE: 'DictionaryGetVariable', 0xAF: 'DictionaryGetMember',
        0xB0: 'DictionaryCallFunctionPop', 0xB1: 'DictionaryCallFunctionSetVariable',
        0xB2: 'DictionaryCallMethodPop', 0xB3: 'DictionaryCallMethodSetVariable'}
STRINGS = {0xA1: 'PushString', 0xA4: 'StringGetVariable', 0xA5: 'StringGetMember',
           0xA6: 'StringSetVariable', 0xA7: 'StringSetMember',
           0x8B: 'SetTarget', 0x8C: 'GoToLabel'}
BRANCHES = {0x99: 'Branch', 0x9D: 'BranchIfTrue', 0xB8: 'BranchIfFalse', 0x94: 'With'}
LIMIT = 100000  # Bounds loops, aggregate records and recursively decoded actions.


class Blob:
    def __init__(self, data):
        self.data = bytes(data)

    def span(self, offset, size):
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f'out-of-bounds record at {offset:#x} ({size} bytes)')
        return self.data[offset:offset + size]

    def word(self, offset):
        return struct.unpack('>I', self.span(offset, 4))[0]

    def string(self, offset, nullable=False):
        if not offset and nullable:
            return ''
        if not offset or offset >= len(self.data):
            raise ValueError(f'invalid string pointer {offset:#x}')
        end = self.data.find(b'\0', offset, min(offset + 65536, len(self.data)))
        if end < 0:
            raise ValueError(f'unterminated string at {offset:#x}')
        return self.data[offset:end].decode('latin1')

    def array(self, offset, count, stride):
        if count > LIMIT or (count and not offset):
            raise ValueError(f'invalid array count/pointer at {offset:#x}')
        self.span(offset, count * stride)
        return range(offset, offset + count * stride, stride)


class Inspector:
    def __init__(self, apt, const):
        self.apt, self.const = Blob(apt), Blob(const)
        if self.apt.span(0, 16) != b'Apt Data:1:7:4\x1a\0':
            raise ValueError('unsupported APT tag (expected decoupled big-endian 1:7:4)')
        if self.const.span(0, 20) != b'Apt constant file\x1a\0\0':
            raise ValueError('invalid constant-file magic')
        self.budget = LIMIT
        self.constants = []
        types = {1: 'string', 3: 'undefined', 4: 'register', 5: 'boolean',
                 6: 'float', 7: 'integer', 8: 'lookup'}
        for at in self.const.array(self.const.word(28), self.const.word(24), 8):
            self.charge()
            typ, raw = self.const.word(at), self.const.word(at + 4)
            if typ not in types:
                raise ValueError(f'unsupported constant type {typ} at {at:#x}')
            if typ == 1: value = self.const.string(raw)
            elif typ == 3: value = None
            elif typ == 5: value = bool(raw)
            elif typ == 6: value = struct.unpack('>f', self.const.span(at + 4, 4))[0]
            elif typ == 7: value = struct.unpack('>i', self.const.span(at + 4, 4))[0]
            else: value = raw
            if typ == 6 and not math.isfinite(value):
                raise ValueError('nonfinite constant float')
            self.constants.append({'type': types[typ], 'value': value})
        self.streams = []

    def charge(self):
        self.budget -= 1
        if self.budget < 0:
            raise ValueError('APT record/action budget exceeded')

    def pool(self, at):
        values = []
        for slot in self.apt.array(self.apt.word(at + 4), self.apt.word(at), 4):
            self.charge()
            index = self.apt.word(slot)
            if index >= len(self.constants):
                raise ValueError(f'invalid constant index {index}')
            values.append(self.constants[index])
        return values

    def actions(self, start, end=None, dictionary=(), depth=0):
        if not start or depth > 32:
            raise ValueError('invalid action pointer or function nesting depth')
        if end is not None:
            self.apt.span(start, end - start)
        p, rows, boundaries = start, [], set()
        while end is None or p < end:
            self.charge()
            offset = p
            op = self.apt.span(p, 1)[0]
            p += 1
            row = {'offset': offset, 'opcode': op}
            if op in SIMPLE:
                row['name'] = SIMPLE[op]
            elif op in (0x88, 0x96):
                p = (p + 3) & ~3
                values = self.pool(p)
                p += 8
                row.update(name='DefineDictionary' if op == 0x88 else 'Push', values=values)
                if op == 0x88: dictionary = values
            elif op in (0x8E, 0x9B):
                p = (p + 3) & ~3
                name, count = self.apt.string(self.apt.word(p), nullable=True), self.apt.word(p + 4)
                p += 8
                if op == 0x8E:
                    registers, flags = struct.unpack('>HH', self.apt.span(p, 4))
                    row.update(register_count=registers, flags=flags)
                    p += 4
                params = []
                for slot in self.apt.array(self.apt.word(p), count, 8 if op == 0x8E else 4):
                    self.charge()
                    param = {'name': self.apt.string(self.apt.word(slot + (4 if op == 0x8E else 0)))}
                    if op == 0x8E: param['register'] = self.apt.word(slot)
                    params.append(param)
                size = self.apt.word(p + 4)
                p += 16  # parameter pointer, size, dead two-word scratch pool
                if end is not None and p + size > end:
                    raise ValueError('function body exceeds enclosing action block')
                body, _ = self.actions(p, p + size, dictionary, depth + 1)
                row.update(name='DefineFunction2' if op == 0x8E else 'DefineFunction',
                           function_name=name, params=params, body_offset=p, body_size=size, body=body)
                p += size
                # Structural assignment pattern; this is not symbolic execution.
                within_block = p < (end if end is not None else len(self.apt.data))
                if within_block and rows and rows[-1]['opcode'] in (0xA2, 0xA3) and self.apt.span(p, 1) == b'\x4f':
                    previous = rows[-1]['value']
                    if previous['type'] == 'string': row['assigned_member'] = previous['value']
            elif op in DICT:
                width = 2 if op == 0xA3 else 1
                index = int.from_bytes(self.apt.span(p, width), 'big')
                p += width
                if index >= len(dictionary):
                    raise ValueError(f'invalid dictionary index {index} at {offset:#x}')
                row.update(name=DICT[op], index=index, value=dictionary[index])
            elif op in STRINGS or op == 0x83:
                p = (p + 3) & ~3
                row.update(name=STRINGS.get(op, 'GetUrl'), value=self.apt.string(self.apt.word(p), nullable=True))
                p += 4
                if op == 0x83:
                    row['target_name'] = self.apt.string(self.apt.word(p), nullable=True)
                    p += 4
            elif op in (0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0x77):
                width = {0xB4: 4, 0xB5: 1, 0xB6: 2, 0xB7: 4, 0xB9: 1, 0x77: 4}[op]
                raw = self.apt.span(p, width)
                p += width
                value = struct.unpack('>f', raw)[0] if op == 0xB4 else int.from_bytes(
                    raw, 'big', signed=op in (0xB5, 0xB6, 0xB7))
                if op == 0xB4 and not math.isfinite(value):
                    raise ValueError('nonfinite immediate float')
                row.update(name={0xB4: 'PushFloat', 0xB5: 'PushByte', 0xB6: 'PushWord',
                                 0xB7: 'PushDWord', 0xB9: 'PushRegister', 0x77: 'TraceStart'}[op],
                           value=value, raw=raw.hex())
            elif op in BRANCHES or op in (0x81, 0x87, 0x9F):
                p = (p + 3) & ~3
                value = struct.unpack('>i', self.apt.span(p, 4))[0]
                p += 4
                if op in BRANCHES:
                    row.update(name=BRANCHES[op], target=p + value)
                else:
                    row.update(name={0x81: 'GoToFrame', 0x87: 'StoreRegister', 0x9F: 'GoToFrame2'}[op], value=value)
            else:
                raise ValueError(f'unsupported opcode {op:#x} at {offset:#x}')
            if end is not None and p > end:
                raise ValueError('instruction exceeds function extent')
            boundaries.add(offset)
            rows.append(row)
            if op == 0 and end is None:
                break
        boundaries.add(p)
        for row in rows:
            if row['opcode'] in BRANCHES and row['target'] not in boundaries:
                raise ValueError(f'branch target is not an instruction boundary at {row["offset"]:#x}')
        return rows, p

    def stream(self, offset, context):
        rows, end = self.actions(offset)
        self.streams.append({'offset': offset, 'end': end, 'context': context, 'instructions': rows})

    def frames(self, offset, count, character):
        frames = []
        for frame, at in enumerate(self.apt.array(offset, count, 8)):
            self.charge()
            controls = []
            for index, slot in enumerate(self.apt.array(self.apt.word(at + 4), self.apt.word(at), 4)):
                self.charge()
                control = self.apt.word(slot)
                if not control: raise ValueError('null timeline control')
                typ = self.apt.word(control)
                row = {'offset': control, 'type': typ}
                context = {'character': character, 'frame': frame, 'control': index}
                if typ in (1, 8):
                    context['kind'] = 'action' if typ == 1 else 'init_action'
                    if typ == 8: context['sprite'] = self.apt.word(control + 4)
                    action = self.apt.word(control + (8 if typ == 8 else 4))
                    self.stream(action, context)
                    row['actions_offset'] = action
                elif typ in (3, 9):
                    self.apt.span(control, 76 if typ == 9 else 64)
                    flags = self.apt.word(control + 4)
                    row.update(flags=flags, depth=self.apt.word(control + 8), character=self.apt.word(control + 12))
                    if flags & 0x04:
                        row['matrix'] = list(struct.unpack('>6f', self.apt.span(control + 16, 24)))
                        if not all(math.isfinite(v) for v in row['matrix']):
                            raise ValueError('nonfinite placement matrix')
                    if flags & 0x08:
                        row['color_scale'] = self.apt.word(control + 40)
                        row['color_bias'] = self.apt.word(control + 44)
                    if flags & 0x10:
                        row['ratio'] = struct.unpack('>f', self.apt.span(control + 48, 4))[0]
                        if not math.isfinite(row['ratio']): raise ValueError('nonfinite placement ratio')
                    if flags & 0x20:
                        row['instance_name'] = self.apt.string(self.apt.word(control + 52), nullable=True)
                    if flags & 0x40:
                        row['clip_depth'] = self.apt.word(control + 56)
                    if flags & 0x80:
                        event = self.apt.word(control + 60)
                        if not event: raise ValueError('null clip-action pointer')
                        for event_index, ev in enumerate(self.apt.array(self.apt.word(event + 4), self.apt.word(event), 12)):
                            self.charge()
                            event_context = dict(context, kind='clip_action', event=event_index,
                                                 triggers=self.apt.word(ev), key=self.apt.word(ev + 4))
                            self.stream(self.apt.word(ev + 8), event_context)
                    if typ == 9 and self.apt.word(control + 68):
                        raise ValueError('unsupported PlaceObject filter records')
                elif typ == 2:
                    row['label'] = self.apt.string(self.apt.word(control + 4))
                elif typ in (4, 5, 6, 7):
                    row['value'] = self.apt.word(control + 4)
                else:
                    raise ValueError(f'unsupported timeline control {typ} at {control:#x}')
                controls.append(row)
            frames.append(controls)
        return frames

    def inspect(self):
        root = self.const.word(20)
        if not root or self.apt.word(root) != 9:
            raise ValueError('invalid root movie')
        self.apt.span(root, 64)
        movie = dict(offset=root, width=self.apt.word(root + 36), height=self.apt.word(root + 40),
                     ms_per_frame=self.apt.word(root + 44))
        characters = []
        for index, at in enumerate(self.apt.array(self.apt.word(root + 32), self.apt.word(root + 28), 4)):
            self.charge()
            offset = self.apt.word(at)
            row = {'index': index, 'offset': offset}
            if offset:
                self.apt.span(offset, 16)
                typ = self.apt.word(offset)
                row['type'] = typ
                row['decoupled_data'] = self.apt.word(offset + 8)
                if typ in (5, 9):
                    if typ == 9 and offset != root: raise ValueError('unexpected nested root movie')
                    row['frames'] = self.frames(self.apt.word(offset + 20), self.apt.word(offset + 16), index)
                elif typ not in (1, 2, 3, 6, 7, 8, 10, 11, 12):
                    raise ValueError(f'unsupported character type {typ} at {offset:#x}')
            characters.append(row)
        movie['characters'] = characters
        if not characters or characters[0]['offset'] != root or sum(c['offset'] == root for c in characters) != 1:
            raise ValueError('root movie must occupy exactly character slot zero')
        movie['imports'] = []
        for at in self.apt.array(self.apt.word(root + 52), self.apt.word(root + 48), 16):
            self.charge()
            movie['imports'].append({'movie': self.apt.string(self.apt.word(at)),
                                     'name': self.apt.string(self.apt.word(at + 4)),
                                     'character': self.apt.word(at + 8)})
        movie['exports'] = []
        for at in self.apt.array(self.apt.word(root + 60), self.apt.word(root + 56), 8):
            self.charge()
            movie['exports'].append({'name': self.apt.string(self.apt.word(at)), 'character': self.apt.word(at + 4)})
        return {'scope': 'static APT structure and bytecode; execution not observed',
                'format': 'big-endian decoupled 1:7:4',
                'coverage': 'root/sprite timelines, placements, clip/init/frame actions; non-timeline character payloads not decoded',
                'apt_sha256': hashlib.sha256(self.apt.data).hexdigest(),
                'const_sha256': hashlib.sha256(self.const.data).hexdigest(),
                'constants': self.constants, 'movie': movie, 'streams': self.streams}


def inspect_pair(apt, const):
    return Inspector(apt, const).inspect()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apt', type=Path, required=True)
    parser.add_argument('--const', dest='const_file', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.resolve() in (args.apt.resolve(), args.const_file.resolve()):
            raise ValueError('output must differ from both input assets')
        result = inspect_pair(args.apt.read_bytes(), args.const_file.read_bytes())
        encoded = json.dumps(result, indent=2, allow_nan=False) + '\n'
    except (OSError, ValueError) as error:
        parser.exit(1, f'APT inspection failed: {error}\n')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(encoded, encoding='utf-8')
    print(json.dumps({'output': str(args.output), 'streams': len(result['streams']),
                      'characters': len(result['movie']['characters']), 'apt_sha256': result['apt_sha256']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
