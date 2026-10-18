"""Prepare private, hash-pinned original x86 trap/boss combat oracles.

Only this authored extractor is distributable. Extracted game instructions and
damage tables are deliberately written under ignored build/.
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.tools/python-analysis'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = Path(r'C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4\Bin32\bio4.exe')
data = EXE.read_bytes()
sha = hashlib.sha256(data).hexdigest()
assert sha == '19aed4af0ab06a748ff8744d45ac5580fcd6be6b6b7e944b1ab8822a00c8ee4a'
u16 = lambda at: struct.unpack_from('<H', data, at)[0]
u32 = lambda at: struct.unpack_from('<I', data, at)[0]
pe = u32(0x3c)
base = u32(pe + 52)
sections = []
for n in range(u16(pe + 6)):
    p = pe + 24 + u16(pe + 20) + n * 40
    sections.append((base + u32(p + 12), u32(p + 16), u32(p + 20)))

def chunk(at, size):
    for va, extent, raw in sections:
        if va <= at and at + size <= va + extent:
            return data[raw + at - va:raw + at - va + size]
    raise ValueError(hex(at))

def target(at):
    assert chunk(at, 1) in (b'\xe8', b'\xe9')
    return at + 5 + struct.unpack('<i', chunk(at + 1, 4))[0]

assert target(0x40ce14) == 0x5b1a90
assert target(0x4b08ae) == 0x4b03b0
assert target(0x4c3a7b) == 0x4c09f0
power_signature=rb'\x55\x8b\xec\x56\x8b\x75\x0c\x57\x83\xfe\x31\x72.\xbe\x02\x00\x00\x00\x8b\x45\x08\x0f\xb6\x80\x00\x01\x00\x00\x83\xc0\xf0'
matches=list(re.finditer(power_signature,data,re.S))
assert len(matches)==1
assert chunk(0x5b1a90,len(matches[0][0]))==matches[0][0]
assert chunk(0x4aa9c1, 10) == bytes.fromhex('c7 86 fc 00 00 00 01 07 00 00')
assert chunk(0x4b08b3, 6) == bytes.fromhex('29 86 38 0a 00 00')
assert chunk(0x4b0b5c, 10) == bytes.fromhex('c7 86 fc 00 00 00 03 00 00 00')

md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = True
header = '#pragma once\n#include <cstdint>\n#include <array>\nnamespace specialfixture {\nstruct Patch {uint32_t offset,target;};\n'
manifest = {}
def arr(name, values, ty='Patch'):
    values=list(values)
    if not values:
        return f'inline const std::array<{ty},0> {name}{{}};\n'
    return 'inline const ' + ty + ' ' + name + '[]={' + ','.join(values) + '};\n'

def block(name, start, end, code_end=None, tables=()):
    global header
    calls, absolute = [], []
    code_end = end if code_end is None else code_end
    decoded = list(md.disasm(chunk(start, code_end - start), start))
    assert sum(i.size for i in decoded) == code_end - start
    for ins in decoded:
        if ins.mnemonic in ('call', 'jmp') and ins.bytes[0] in (0xe8, 0xe9):
            calls.append((ins.address - start, target(ins.address)))
        for op in ins.operands:
            if op.type == 3 and op.mem.disp >= 0x400000:
                assert ins.disp_size == 4
                absolute.append((ins.address - start + ins.disp_offset, op.mem.disp))
    for at, count in tables:
        for n in range(count):
            value = struct.unpack('<I', chunk(at + n * 4, 4))[0]
            assert start <= value < code_end
            absolute.append((at + n * 4 - start, value))
    header += f'inline constexpr uint32_t {name}Start={hex(start)};\n'
    header += arr(name + 'Code', (str(v) for v in chunk(start, end-start)), 'uint8_t')
    header += arr(name + 'Calls', ('{' + str(at) + ',' + hex(to) + '}' for at, to in calls))
    header += arr(name + 'Absolute', ('{' + str(at) + ',' + hex(to) + '}' for at, to in absolute))
    manifest[name] = {'start': hex(start), 'end': hex(end), 'externalCalls': sorted(hex(to) for _,to in calls if not start <= to < end), 'absolute': sorted(set(hex(to) for _,to in absolute if not start <= to < end))}

block('Trap', 0x4aa920, 0x4aa9ce)
block('GiganteCalc', 0x4b03b0, 0x4b041d)
block('Gigante', 0x4b07c0, 0x4b0cdc, 0x4b0c36,
      ((0x4b0c38,6),(0x4b0c7c,2),(0x4b0ca8,2)))
block('VerdugoCalc', 0x4c09f0, 0x4c0a3b)
block('SalazarCalc', 0x539950, 0x53999a)
block('Power', 0x5b1a90, 0x5b1d78, 0x5b1c78, ((0x5b1c78,64),))
# Keep the exact original SSE conversion branch, including its feature-flag
# test; the fixture selects the actual supported SSE branch with flag=1.
block('Convert', 0xa6ecf0, 0xa6ed0c)
block('Setter', 0x5aeef0, 0x5aef50)
header += arr('PowerTables', (str(v) for v in chunk(0xc001b8,0xf14)), 'uint8_t')
constants = sorted({int(at,16) for b in manifest.values() for at in b['absolute'] if 0xb00000 <= int(at,16) < 0xc00000})
header += 'struct Constant {uint32_t address; uint64_t bits;};\n'
header += arr('Constants', ('{'+hex(at)+','+hex(struct.unpack('<Q',chunk(at,8))[0])+'ULL}' for at in constants),'Constant')
header += '}\n'
build = ROOT / 'build'
build.mkdir(exist_ok=True)
(build / 'native-special-combat-fixture-private.hpp').write_text(header)
proof = {'exeSha256':sha, 'blocks':manifest,
         'uniqueProductionPowerSignatureVerified':True,
         'fixtureScope':'Actual original x86 trap consumer, complete El Gigante damage consumer, actual boss damage calculators and original shared weapon-power logic/tables. Only external engine services recorded.',
         'liveGameplayVerified':False}
(build / 'native-special-combat-offline-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print('PASS: hash-pinned original trap/Gigante consumer, three boss calculators and shared weapon-power oracle extracted privately.')
