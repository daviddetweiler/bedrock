import sys
from typing import *
from collections import defaultdict

OPCODES = {
    'jmp': 0x0,
    'rhi': 0x1,
    'set': 0x2,
    'lod': 0x3,
    'sto': 0x4,
    'add': 0x5,
    'sub': 0x6,
    'mul': 0x7,
    'div': 0x8,
    'shl': 0x9,
    'shr': 0xa,
    'and': 0xb,
    'lor': 0xc,
    'not': 0xd,
    'bsr': 0xe,
    'bsw': 0xf
}

REGISTERS = {
    'r0': 0x0,
    'r1': 0x1,
    'r2': 0x2,
    'r3': 0x3,
    'r4': 0x4,
    'r5': 0x5,
    'r6': 0x6,
    'r7': 0x7,
    'r8': 0x8,
    'r9': 0x9,
    'ra': 0xa,
    'rb': 0xb,
    'rc': 0xc,
    'rd': 0xd,
    're': 0xe,
    'rf': 0xf
}


class Eof(Exception):
    pass


class NotReg(Exception):
    def __init__(self, token: str):
        self.token = token


class NotNum(Exception):
    def __init__(self, token: str):
        self.token = token


def next(tokens: List[str]) -> Union[str, None]:
    if len(tokens):
        return tokens.pop(0)
    else:
        raise Eof()


def pack(a: int, b: int, c: int, d: int) -> int:
    assert a in range(16)
    assert b in range(16)
    assert c in range(16)
    assert d in range(16)
    packed = a << 12 | b << 8 | c << 4 | d
    assert packed in range(1 << 16)
    return packed


def register(token: str):
    if token not in REGISTERS:
        raise NotReg(token)

    return REGISTERS[token]


labels = {}
backrefs = defaultdict(lambda: [])
BASE = 0x28


def encode(instr: str, tokens: List[str], stream: List[int]):
    encoded = OPCODES[instr]
    if instr == 'jmp':
        stream.append(pack(encoded, register(next(tokens)),
                      register(next(tokens)), register(next(tokens))))
    elif instr == 'rhi':
        stream.append(pack(encoded, register(next(tokens))))
    elif instr == 'set':
        dst = register(next(tokens))
        token = next(tokens)
        try:
            value = int(token, base=16)
            stream.append(pack(encoded, dst, value >> 4, value & 0xf))
        except ValueError:
            if token in labels:
                addr = labels[token]
                lo = addr & 0xff
                hi = addr >> 8
                if hi:
                    stream.append(pack(encoded, dst, hi >> 4, hi & 0xf))
                    stream.append(pack(OPCODES['shr'], dst, 4, dst))
                    stream.append(
                        pack(OPCODES['set'], REGISTERS['rf'], lo >> 4, lo & 0xf))
                    stream.append(
                        pack(OPCODES['lor'], dst, dst, REGISTERS['rf']))
                else:
                    stream.append(pack(encoded, dst, lo >> 4, lo & 0xf))
            else:
                print(f'Warning: using forward ref \'{token}\'')
                backrefs[token].append(len(stream))
                stream.append(pack(encoded, dst, 0, 0))
                stream.append(pack(OPCODES['shr'], dst, 4, dst))
                stream.append(pack(OPCODES['set'], REGISTERS['rf'], 0, 0))
                stream.append(pack(OPCODES['lor'], dst, dst, REGISTERS['rf']))
    elif instr == 'lod':
        stream.append(pack(encoded, register(
            next(tokens)), 0, register(next(tokens))))
    elif instr == 'sto':
        stream.append(pack(encoded, 0, register(
            next(tokens)), register(next(tokens))))
    elif instr == 'add':
        stream.append(pack(encoded, register(next(tokens)),
                      register(next(tokens)), register(next(tokens))))
    elif instr == 'sub':
        stream.append(pack(encoded, register(next(tokens)),
                      register(next(tokens)), register(next(tokens))))
    elif instr == 'mul':
        stream.append(pack(encoded, register(next(tokens)),
                      register(next(tokens)), register(next(tokens))))
    elif instr == 'div':
        stream.append(pack(encoded, register(next(tokens)),
                      register(next(tokens)), register(next(tokens))))
    elif instr == 'shl':
        dst = register(next(tokens))
        token = next(tokens)
        try:
            value = int(token, base=16)
            stream.append(pack(encoded, dst, value, register(next(tokens))))
        except ValueError:
            raise NotNum(token)
    elif instr == 'shr':
        dst = register(next(tokens))
        token = next(tokens)
        try:
            value = int(token, base=16)
            stream.append(pack(encoded, dst, value, register(next(tokens))))
        except ValueError:
            raise NotNum(token)
    elif instr == 'and':
        stream.append(pack(encoded, register(next(tokens)),
                      register(next(tokens)), register(next(tokens))))
    elif instr == 'lor':
        stream.append(pack(encoded, register(next(tokens)),
                      register(next(tokens)), register(next(tokens))))
    elif instr == 'not':
        stream.append(pack(encoded, register(
            next(tokens)), 0, register(next(tokens))))
    elif instr == 'bsr':
        stream.append(pack(encoded, register(
            next(tokens)), 0, register(next(tokens))))
    elif instr == 'bsw':
        stream.append(pack(encoded, 0, register(
            next(tokens)), register(next(tokens))))


def patch(stream: List[int], offset: int, addr: int):
    stream[offset] |= addr >> 8
    stream[offset + 2] |= addr & 0xff


def main():
    if len(sys.argv) != 3:
        print('Usage: assemble.py <source> <output>')
        return

    source = open(sys.argv[1], 'r')
    out = open(sys.argv[2], 'w')
    tokens = ''.join(source.readlines()).split()
    stream = []
    try:
        while len(tokens):
            token = next(tokens)
            if token[-1] == ':':  # is a label
                label = token[:-1]
                addr = BASE + len(stream)
                labels[label] = addr
                if label in backrefs:
                    for ref in backrefs[label]:
                        patch(stream, ref, addr)
            elif token in OPCODES:
                encode(token, tokens, stream)
            else:
                print('Bad token:', token)
                return
    except Eof:
        print('Unexpected EOF')
    except NotReg as err:
        print(f'{err.token} is not a register')
    except NotNum as err:
        print(f'{err.token} is not a number')

    for value in stream:
        num = hex(value)[2:]
        pad = '0' * (4 - len(num))
        print(pad + num, file=out)


if __name__ == '__main__':
    main()
