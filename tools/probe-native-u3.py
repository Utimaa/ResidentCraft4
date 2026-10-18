"""Prepare private retail x86 U-3 area-trigger oracles from owned local files.

All extracted instructions and the original room record remain in ignored build/.
This tool never launches, attaches to, or writes a game or save.
"""
from pathlib import Path
import argparse, hashlib, json, struct, sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.tools/python-analysis'))
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--aev',type=Path,default=ROOT/'build/u3-room-assets-private/15.AEV')
args=parser.parse_args()
exe=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4\Bin32\bio4.exe')
data=exe.read_bytes();sha=hashlib.sha256(data).hexdigest()
assert sha=='19aed4af0ab06a748ff8744d45ac5580fcd6be6b6b7e944b1ab8822a00c8ee4a'
u16=lambda at:struct.unpack_from('<H',data,at)[0]
u32=lambda at:struct.unpack_from('<I',data,at)[0]
pe=u32(0x3c);base=u32(pe+52);sections=[]
for i in range(u16(pe+6)):
    at=pe+24+u16(pe+20)+i*40;sections.append((base+u32(at+12),u32(at+16),u32(at+20)))
def chunk(at,size):
    for va,extent,raw in sections:
        if va<=at and at+size<=va+extent:return data[raw+at-va:raw+at-va+size]
    raise ValueError(hex(at))
def target(at):
    assert chunk(at,1) in (b'\xe8',b'\xe9')
    return at+5+struct.unpack('<i',chunk(at+1,4))[0]
def resolved(at):
    return target(at) if chunk(at,1)==b'\xe9' else at

# Room initialization registers area event3 and the actual original intro;
# no fabricated spawn call or room-save flag is introduced.
assert chunk(0x7b633b,11)==bytes.fromhex('6a 01 53 68 00 57 7b 00 53 6a 14')
assert chunk(0x7b6346,2)==bytes.fromhex('6a 03')
assert resolved(target(0x7b6348))==0x6b4c60
assert chunk(0x7b5751,7)==bytes.fromhex('f7 40 04 00 00 00 10')
assert resolved(target(0x7b5972))==0x6c1470
assert chunk(0x6c1531,10)==bytes.fromhex('81 88 1c 50 00 00 00 10 00 00')
assert chunk(0x6c1736,10)==bytes.fromhex('81 a0 1c 50 00 00 ff ef ff ff')
assert resolved(target(0x7b549e))==0x6c1680
assert chunk(0x6bb19a,12)==bytes.fromhex('8b 90 98 00 00 00 8b 88 94 00 00 00')
assert target(0x6bb2a3)==0x6b7db0

aev=args.aev.read_bytes()
assert hashlib.sha256(aev).hexdigest()=='3f439590bff1547648a8a41c2e7a03e611c7ddc4f93e01284d13299e98735191'
assert aev[:4]==b'AEV\0' and struct.unpack_from('<H',aev,6)[0]==14
assert len(aev)==2208
record=aev[16+3*0x9c:16+4*0x9c]
assert record[0x34:0x3a]==bytes.fromhex('03 02 03 00 01 01')
assert struct.unpack_from('<I',record,0x4c)[0]==0

md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
header='#pragma once\n#include <cstdint>\n#include <array>\nnamespace u3fixture { struct Patch {uint32_t offset,target;};\n'
manifest={};constants=set()
def arr(name,items,ty):
    values=list(items)
    return ('inline const '+ty+' '+name+'[]={'+','.join(values)+'};\n') if values else 'inline const std::array<'+ty+',0> '+name+'{};\n'
for name,start,end in [('Copy',0x6b3540,0x6b3728),('Predicate',0x6b7db0,0x6b7f7f),
                       ('Dispatch',0x58b440,0x58b472),('Quad',0x58adb0,0x58ae96)]:
    calls=[];absolute=[];instructions=list(md.disasm(chunk(start,end-start),start))
    assert sum(i.size for i in instructions)==end-start
    for ins in instructions:
        if ins.mnemonic in ('call','jmp') and ins.bytes[0] in (0xe8,0xe9):
            calls.append((ins.address-start,resolved(target(ins.address))))
        for op in ins.operands:
            if op.type==3 and op.mem.disp>=0x400000:
                assert ins.disp_size==4
                absolute.append((ins.address-start+ins.disp_offset,op.mem.disp))
                if 0xb00000<=op.mem.disp<0xc00000:constants.add(op.mem.disp)
    header+=f'inline constexpr uint32_t {name}Start={hex(start)};\n'
    header+=arr(name+'Code',(str(v) for v in chunk(start,end-start)),'uint8_t')
    header+=arr(name+'Calls',('{'+str(at)+','+hex(to)+'}' for at,to in calls),'Patch')
    header+=arr(name+'Absolute',('{'+str(at)+','+hex(to)+'}' for at,to in absolute),'Patch')
    manifest[name]={'start':hex(start),'end':hex(end),'externalCalls':[hex(to) for _,to in calls]}
constants.add(0xb0fc38)
header+='struct Constant {uint32_t address;uint64_t bits;};\n'
header+=arr('Constants',('{'+hex(at)+','+hex(struct.unpack('<Q',chunk(at,8))[0])+'ULL}' for at in sorted(constants)),'Constant')
header+=arr('AreaEvent3',(str(v) for v in record),'uint8_t')+'}\n'
(ROOT/'build/native-u3-fixture-private.hpp').write_text(header)
proof={'exeSha256':sha,'aevSha256':hashlib.sha256(aev).hexdigest(),'room':'0x50f','areaEvent':3,
       'registration':'0x7b633b -> original AEV setter0x6b4c60, callback0x7b5700',
       'eventBegin':'0x7b5972 -> 0x6c1470; STA_EVENT set0x6c1531 before explicit yield0x7b5984',
       'eventEnd':'0x7b549e -> 0x6c1680; STA_EVENT cleared0x6c1736',
       'scriptedPlayerPlacements':['0x7b59dd','0x7b59ed','0x7b5bf3','0x7b5c03'],
       'actorPredicate':'0x6bb180 reads pos94 then calls original geometry0x6b7db0; no old110/speed104/DPF_PL gate',
       'blocks':manifest,'liveEncounterVerified':False}
(ROOT/'build/native-u3-offline-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print('PASS: hash-pinned original U-3 event registration, STA_EVENT placement interval and private x86 polygon fixture prepared.')
