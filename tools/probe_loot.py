"""Read-only validation of actual chapter-loot engine seed/reload logs and saved NBT.

The old five-tier synthetic-peer probe is retired. Run the guarded
re4craft.lootProgressionTest=seed/check client fixture in the same disposable
build/release-review/<32 hex>/.instance profile, stop each client gracefully,
then invoke this validator. This program never launches or connects to a game.
"""
from __future__ import annotations
import argparse, hashlib, json, re, struct, zlib
from pathlib import Path

MAX_NBT = 8 * 1024 * 1024
GEAR_TAG = 're4craft:loot_qa_crafted_v2'
BOOK_TAG = 're4craft:loot_qa_book_v2'
EXPECTED = {
    'netherite_sword': {'sharpness': 5, 'unbreaking': 3, 'mending': 1},
    'netherite_pickaxe': {'efficiency': 5, 'unbreaking': 3, 'mending': 1, 'fortune': 3},
    'netherite_helmet': {'protection': 4, 'unbreaking': 3, 'mending': 1},
    'netherite_chestplate': {'protection': 4, 'unbreaking': 3, 'mending': 1},
    'netherite_leggings': {'protection': 4, 'unbreaking': 3, 'mending': 1},
    'netherite_boots': {'protection': 4, 'unbreaking': 3, 'mending': 1, 'feather_falling': 4},
    'bow': {'power': 5, 'unbreaking': 3, 'infinity': 1},
}

class NbtReader:
    def __init__(self, data):
        if len(data) > MAX_NBT: raise ValueError('NBT exceeds review bound')
        self.data = data; self.at = 0
    def take(self, n):
        if n < 0 or self.at+n > len(self.data): raise ValueError('Truncated NBT')
        value=self.data[self.at:self.at+n]; self.at+=n; return value
    def number(self, fmt): return struct.unpack('>'+fmt,self.take(struct.calcsize('>'+fmt)))[0]
    def string(self): return self.take(self.number('H')).decode('utf-8')
    def payload(self, kind, depth=0):
        if depth > 64: raise ValueError('NBT nesting bound')
        if kind in (1,2,3,4,5,6): return self.number({1:'b',2:'h',3:'i',4:'q',5:'f',6:'d'}[kind])
        if kind == 8: return self.string()
        if kind == 10:
            result={}
            while True:
                kind=self.number('B')
                if kind==0: return result
                name=self.string()
                if name in result: raise ValueError('Duplicate NBT name')
                result[name]=self.payload(kind,depth+1)
        if kind in (7,9,11,12):
            child=self.number('B') if kind==9 else {7:1,11:3,12:4}[kind]
            count=self.number('i')
            if count<0 or count>1000000: raise ValueError('NBT collection bound')
            return [self.payload(child,depth+1) for _ in range(count)]
        raise ValueError('Unsupported NBT type')
    def root(self):
        kind=self.number('B'); self.string(); value=self.payload(kind)
        if self.at != len(self.data): raise ValueError('Trailing NBT bytes')
        return value

def decompress(data, window):
    stream=zlib.decompressobj(window); result=stream.decompress(data,MAX_NBT+1)
    if len(result)>MAX_NBT or stream.unconsumed_tail or not stream.eof: raise ValueError('Compressed NBT exceeds bound/truncated')
    return result

def saved_dat(path): return NbtReader(decompress(path.read_bytes(),31)).root()

def region_entities(path):
    if path.is_symlink() or path.stat().st_size > 64*1024*1024: raise ValueError('Invalid QA region')
    data=path.read_bytes()
    if len(data)<8192: raise ValueError('Short region header')
    for slot in range(1024):
        offset=int.from_bytes(data[slot*4:slot*4+3],'big')*4096; sectors=data[slot*4+3]
        if not offset and not sectors: continue
        if offset<8192 or sectors<1 or offset+sectors*4096>len(data): raise ValueError('Invalid region offset')
        size=int.from_bytes(data[offset:offset+4],'big'); compression=data[offset+4]
        if size<2 or size+4>sectors*4096 or compression not in (1,2,3): raise ValueError('Invalid region chunk')
        raw=data[offset+5:offset+4+size]
        chunk=NbtReader(raw if compression==3 else decompress(raw,31 if compression==1 else 15)).root()
        yield from chunk.get('Entities',chunk.get('entities',[]))

def inspect_world(world):
    world=world.resolve(strict=True); instance=world.parent.parent; lab=instance.parent
    if world.parent.name!='saves' or instance.name!='.instance' or not re.fullmatch('[a-f0-9]{32}',lab.name) or lab.parent.name!='release-review' or lab.parent.parent.name!='build':
        raise ValueError('Saved NBT review is restricted to the disposable release-review instance')
    ledgers=list(world.rglob('host_loot.dat'))
    if len(ledgers)!=1: raise ValueError('Exactly one actual host loot ledger required')
    ledger=saved_dat(ledgers[0]); ledger=ledger.get('data',ledger)
    regions=list(world.glob('entities/*.mca'))+list(world.glob('dimensions/**/entities/*.mca'))
    if len(regions)>64: raise ValueError('Too many QA entity regions')
    saved={}; saved_books=[]
    for region in regions:
        for entity in region_entities(region):
            tags=entity.get('Tags',entity.get('tags',[]))
            if GEAR_TAG not in tags and BOOK_TAG not in tags: continue
            item=entity.get('Item',entity.get('item',{})); ident=item.get('id','')
            if BOOK_TAG in tags:
                if ident!='minecraft:enchanted_book': raise ValueError('Unexpected saved QA book item')
                enchants=item.get('components',{}).get('minecraft:stored_enchantments',{});enchants=enchants.get('levels',enchants)
                saved_books.append(dict(count=item.get('count',item.get('Count',0)),enchantments={key.removeprefix('minecraft:'):value for key,value in enchants.items()}));continue
            if not ident.startswith('minecraft:') or ident[10:] not in EXPECTED or ident in saved: raise ValueError('Unexpected/duplicate saved QA crafted gear')
            enchants=item.get('components',{}).get('minecraft:enchantments',{})
            enchants=enchants.get('levels',enchants)
            actual={key.removeprefix('minecraft:'):value for key,value in enchants.items()}
            saved[ident]=dict(count=item.get('count',item.get('Count',0)),enchantments=actual,region=region.relative_to(world).as_posix())
    checks={
        'saved_book_rotation_after_reload': len(ledger.get('book_cycles',[]))==1 and ledger['book_cycles'][0].get('crate')==3,
        'legacy_gear_claims_inactive': not ledger.get('milestones',[]),
        'saved_native_session_stamps': len(ledger.get('sessions',[]))>=2,
        'actual_saved_all_seven_recipe_crafted_items': set(saved)=={'minecraft:'+item for item in EXPECTED},
        'actual_saved_component_levels_exact': bool(saved) and all(s['count']==1 and s['enchantments']==EXPECTED[item[10:]] for item,s in saved.items()),
        'actual_saved_stored_books_exact': len(saved_books)==5 and all(s['count']==1 for s in saved_books) and {tuple(sorted(s['enchantments'].items())) for s in saved_books}=={tuple(sorted(value.items())) for value in EXPECTED.values()},
    }
    return checks,saved,dict(ledgerFile=ledgers[0].relative_to(world).as_posix(),ledgerSha256=hashlib.sha256(ledgers[0].read_bytes()).hexdigest(),legacyMilestones=ledger.get('milestones',[]),bookCycles=ledger.get('book_cycles',[]),storedBooks=saved_books,sessions=ledger.get('sessions',[]))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed-log',required=True,type=Path); parser.add_argument('--reload-log',required=True,type=Path)
    parser.add_argument('--saved-world',required=True,type=Path); parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args(); checks={}
    for phase,path in (('seed',args.seed_log),('check',args.reload_log)):
        if path.is_symlink() or path.stat().st_size>32*1024*1024: raise ValueError('Invalid QA log')
        log=path.read_text('utf-8',errors='replace')
        match=re.search(r'LOOT_PROGRESSION_QA phase='+phase+r' passed=true checks=(\d+)',log)
        checks['actual_engine_'+phase+'_passed']=bool(match) and int(match[1])>=70
        checks['actual_engine_'+phase+'_no_failure']='LOOT_PROGRESSION_QA phase='+phase+' passed=false' not in log
        # Minecraft26.3 logs the owned receipt and each normal save phase;
        # its integrated-server shutdown has no "All dimensions are saved"
        # marker. The saved NBT checks below establish completed persistence.
        receipt='Owned launcher requested graceful Minecraft shutdown; saving the selected world'
        shutdown=log[log.rfind(receipt):] if receipt in log else ''
        positions=[shutdown.find(marker) for marker in (receipt,'Stopping server','Saving players','Saving worlds','Saving chunks for level')]
        checks['actual_engine_'+phase+'_graceful_save']=all(at>=0 for at in positions) and positions==sorted(positions)
    seed=args.seed_log.read_text('utf-8',errors='replace')
    budget=re.search(r'LOOT_RECIPE_QA crates=16 .*?minimumRealXp=(\d+) requiredVanillaAnvilXp=(\d+) actualCraftedGear=7 actualStoredBooks=5',seed)
    checks['actual_recipe_resource_xp_budget']=bool(budget) and int(budget[1])>=int(budget[2]) and int(budget[2])>0
    experience=re.search(r'LOOT_EXPERIENCE_QA phase=seed passed=true .*?actualBottleImpact=true importedGround=true groundedTicks=(\d+) vanillaPickup=true .*?received=(\d+)',seed)
    checks['actual_bottle_orb_floor_pickup']=bool(experience) and int(experience[1])>=5 and 3<=int(experience[2])<=11
    persisted,items,ledger=inspect_world(args.saved_world);checks.update(persisted)
    proof=dict(passed=all(checks.values()),checks=checks,actualSavedRecipeCraftedItems=items,savedLedger=ledger,
               scope='Read-only NBT written by actual Minecraft26.3 after real integrated-server item/enchantment, paid crafting/smithing/anvil recipes, partial-spawn rollback, legacy codec migration, source-specific book rotations, save/reload and replay checks. Native crate break/drop hooks require separate native/live checks.')
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(proof,indent=2),encoding='utf-8')
    print(json.dumps(proof,indent=2))
    if not proof['passed']: raise SystemExit(1)

if __name__=='__main__': main()

