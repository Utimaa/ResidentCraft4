"""Hash-pinned original RE4 box consumer oracle. Extracted bytes stay private in build/."""
import hashlib,json,re,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'.tools/python-analysis'))
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
EXE=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4\Bin32\bio4.exe')
data=EXE.read_bytes();sha=hashlib.sha256(data).hexdigest()
assert sha=='19aed4af0ab06a748ff8744d45ac5580fcd6be6b6b7e944b1ab8822a00c8ee4a'
u16=lambda p:struct.unpack_from('<H',data,p)[0]
u32=lambda p:struct.unpack_from('<I',data,p)[0]
pe=u32(0x3c);base=u32(pe+52);sections=[]
for i in range(u16(pe+6)):
 p=pe+24+u16(pe+20)+40*i
 sections.append((data[p:p+8].rstrip(b'\0'),base+u32(p+12),u32(p+16),u32(p+20)))
def chunk(address,length):
 for _,va,size,raw in sections:
  if va<=address and address+length<=va+size:return data[raw+address-va:raw+address-va+length]
 raise ValueError(hex(address))
def target(address):
 b=chunk(address,5);assert b[0] in (0xe8,0xe9)
 return address+5+struct.unpack_from('<i',b,1)[0]
start,end=0x5c39d0,0x5c4019
assert chunk(0x5c3741,10)==bytes.fromhex('c7 86 24 03 00 00 e8 03 e8 03')
assert target(0x5c3edd)==0x40718f and target(0x40718f)==0x5bc3e0
assert target(0x5bc3f1)==0x4071c1
assert target(0x4071c1)==0x5b5af0
assert chunk(0x5c3ed4,5)==bytes.fromhex('b9 e8 03 00 00')
assert chunk(0x5c3f0c,5)==bytes.fromhex('b9 0f 27 00 00')
assert target(0x5c3f36)==start and target(0x5c3cb5)==0x40721b
md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
calls=[];absolute=[]
for low,high in [(start,0x5c3cca),(0x5c3dd0,0x5c3f72)]:
 for ins in md.disasm(chunk(low,high-low),low):
  if ins.mnemonic=='call':
   assert ins.bytes[0]==0xe8
   calls.append((ins.address-start,target(ins.address)))
  for op in ins.operands:
   if op.type==3 and op.mem.disp>=0x400000:
    assert ins.disp_size==4
    absolute.append((ins.address-start+ins.disp_offset,op.mem.disp))
tables=[(0x5c3ccc,5),(0x5c3f74,2),(0x5c3f84,2),(0x5c3fac,3),(0x5c3fe4,3)]
for address,count in tables:
 for i in range(count):
  at=address+4*i;value=struct.unpack('<I',chunk(at,4))[0]
  assert start<=value<end
  absolute.append((at-start,value))
external=sorted(set(value for _,value in absolute if not start<=value<end))
assert external==[0xb10354,0xc35e10],external
known={0x4014c4,0x40163b,0x40adc1,0x40721b,0x405c8b,0x40718f,0x406415,0xa6cd9a}
assert all(start<=to<end or to in known for _,to in calls)
build=ROOT/'build';build.mkdir(exist_ok=True)
def array(name,items):return f'inline const Patch {name}[]={{'+','.join('{'+str(at)+','+hex(value)+'}' for at,value in items)+'};\n'
header='#pragma once\n#include <cstdint>\nnamespace boxfixture { struct Patch {uint32_t offset,target;};\n'
header+=f'inline constexpr uint32_t OriginalStart={hex(start)}, OriginalEnd={hex(end)}, ConsumerOffset={hex(0x5c3dd0-start)};\n'
header+='inline const uint8_t Code[]={'+','.join(str(n) for n in chunk(start,end-start))+'};\n'
header+=array('Calls',calls)+array('Absolute',absolute)
header+=f'inline constexpr uint32_t DistanceLimitBits={hex(struct.unpack("<I",chunk(0xb10354,4))[0])};\n}}\n'
assert chunk(0x5aeef0,16)==bytes.fromhex('55 8b ec 8a 45 08 d9 ee 0c 01 56 8b f1 8a 4d 0c')
assert chunk(0x5aef4d,1)==bytes.fromhex('c2')
header+='namespace boxfixture { inline const uint8_t DamageSetterCode[]={'+','.join(str(n) for n in chunk(0x5aeef0,0x60))+'}; }\n'
assert chunk(0x5c3249,2)==bytes.fromhex('6a 53')
assert struct.unpack('<7I',chunk(0x5c3828,28))==(0x5c3336,0x5c3381,0x5c33cc,0x5c3417,0x5c342b,0x5c3476,0x5c34be)
assert target(0x5c40b3)==0x5c3dd0
move_pattern=rb'\x56\x8b\xf1\xe8....\x0f\xb6\x86\xfc\x00\x00\x00\x81\x66\x04\xff\xbf\xff\xff\x8b\x0c\x85....\x56\xff\xd1\xa1....\xba\x27\x02\x00\x00'
assert len(list(re.finditer(move_pattern,data,re.S)))==1
assert chunk(0x5c3c95,12)==bytes.fromhex('83 c8 ff 39 86 2c 06 00 00 74 1d 0f')
dimensions=[(0xb0f7b8,0xb0f8c0,0xb0f7c0),(0xb0f7a4,0xb10434,0xb109a4),
 (0xb1729c,0xb10f58,0xb1729c),(0xb0f7a4,0xb10434,0xb109a4),
 (0xb0fac0,0xb10b4c,0xb0f8c0),(0xb0f92c,0xb0f950,0xb0f92c),(0xb109a4,0xb12400,0xb0f7c0)]
profiles=[tuple(struct.unpack('<f',chunk(a,4))[0] for a in row) for row in dimensions]
header+='namespace boxfixture { inline const float BodyExtents[7][3]={'+','.join('{'+','.join(str(v)+'f' for v in row)+'}' for row in profiles)+'}; }\n'
script_start,script_end=0x5c2a40,0x5c2ab1
script_calls=[];script_absolute=[]
for ins in md.disasm(chunk(script_start,script_end-script_start),script_start):
 if ins.mnemonic=='call':script_calls.append((ins.address-script_start,target(ins.address)))
 for op in ins.operands:
  if op.type==3 and op.mem.disp>=0x400000:script_absolute.append((ins.address-script_start+ins.disp_offset,op.mem.disp))
assert script_calls==[(0x25,0x40eac0),(0x46,0x40bdde)]
assert script_absolute==[(0x11,0xc06f3c)]
assert chunk(0x5c2aa1,6)==bytes.fromhex('fe 86 fe 00 00 00')
header+='namespace boxfixture { inline const uint8_t BrokenScriptCode[]={'+','.join(str(n) for n in chunk(script_start,script_end-script_start))+'};\n'
header+=array('BrokenScriptCalls',script_calls)+array('BrokenScriptAbsolute',script_absolute)+'}\n'
(build/'native-box-fixture-private.hpp').write_text(header)
source=(ROOT/'native/guest_controller.cpp').read_text()
assert 'readEnemy(address,enemy)&&!nativeBreakableKind(enemy.kind)&&damageIdentity' in source
assert 'if(nativeReaction&&!nativeBreakableKind(e.kind))' in source
proof={'exeSha256':sha,'kind':83,'upstream':'reference/re4_tweaks/dllmain/Game.cpp EmNames Box 0x53',
 'initialHPAndMax':1000,'subtypes':list(range(1,8)),'originalBodyExtents':profiles,
 'actualConstructor':'0x5c3230','originalMove':'0x5c40b0','originalConsumer':'0x5c3dd0','originalDestroy':'0x5c39d0',
 'ordinaryDamage':1000,'heavyDamage':9999,'lifeDown3ArgWrapper':'0x5bc3e0',
 'lifeDown4ArgConsumer':'0x5b5af0','originalItemSetCall':'0x5c3cb5',
 'nativeDestructionRetained':True,'scaledCreatureLedgerExcluded':True,
 'allSubtypeDropsConvergeAt':'0x5c3cb5','emptyDropSkippedWhen':'actor+0x62c == 0xffffffff',
 'emptyRewardObserver':'verified cEmBox move; original HP-positive to HP-zero and low-word broken routine0x0101, preserving FE native phase; actor generation and ordinary item flags',
 'brokenStateScript':'0x5c2a40; actual native save/collision/control bookkeeping and FE phase increment executed by fixture',
 'excludedObjects':{'explosiveBarrelKind':'0x58, separate scripted explosion consumer','cObjBox':'0xb1d9c4; visual object update has no crate hit/loot consumer'},
 'fixture':'private original native consumer/destructor bytes with external game calls replaced by argument-recording stubs',
 'liveGameplayVerified':False}
(build/'native-minecraft-box-offline-proof.json').write_text(json.dumps(proof,indent=2)+'\n')
print('PASS: actual retail Box kind0x53, all seven constructor variants/full body extents, 1000 HP, original 1000/9999 consumer, native destruction and common ItemSet; private x86 oracle prepared.')
