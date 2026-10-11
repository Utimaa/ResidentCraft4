// Real x86 retail consumer/destructor bytes are extracted from the user's
// hash-pinned executable into ignored build/. Only external engine services
// are replaced with ABI-matching recording callbacks; no fake break routine.
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#define private public
#include "../native/guest_controller.hpp"
#undef private
#include "../build/native-box-fixture-private.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
using namespace re4craft;
namespace {
unsigned checks{},healthCalls{},dropCalls{},fragments{},sounds{},effects{};
int nativeDamage{},appliedDamage{};uint32_t itemId{},itemCount{};uintptr_t itemPosition{};
bool simulateOldLedger{};CombatDamageLedger ledger;
int worldHazard{};
uint16_t savedCrateFlag{};unsigned nativeBrokenControlCalls{};
void check(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class T>T field(const uint8_t* actor,size_t at){T value{};std::memcpy(&value,actor+at,sizeof(value));return value;}
template<class T>void field(uint8_t* actor,size_t at,T value){std::memcpy(actor+at,&value,sizeof(value));}
int __cdecl health(void* model,int amount,int randomRange){
    auto actor=static_cast<uint8_t*>(model);++healthCalls;nativeDamage=amount;
    check(randomRange==0,"original crate consumer changed native random range");
    const auto kind=field<uint8_t>(actor,0x100);
    if(simulateOldLedger||!nativeBreakableKind(kind)){
        NativeDamageIdentity identity{field<uint8_t>(actor,0x32e),field<uint32_t>(actor,0x340),field<Vec3>(actor,0x330)};
        int selected{};const auto claim=ledger.consume(7,18,reinterpret_cast<uintptr_t>(model),identity,100,selected);
        if(claim==DamageClaim::Apply)amount=selected;
    }
    appliedDamage=amount;auto hp=field<int16_t>(actor,0x324);hp=static_cast<int16_t>(std::max(0,int(hp)-amount));field(actor,0x324,hp);return hp;
}
int __cdecl itemSet(const Vec3* position,uint32_t id,uint32_t count,int effect,int saveIndex,int opaque,int other){
    ++dropCalls;itemPosition=reinterpret_cast<uintptr_t>(position);itemId=id;itemCount=count;
    check(effect==-1&&saveIndex==-1&&opaque==0&&other==-1,"original seven-argument drop ABI changed");return -1;
}
void __cdecl fragment(){++fragments;}
void __cdecl effect(){++effects;}
void __cdecl sound(){++sounds;}
uint16_t* __cdecl savedCrate(uint32_t,uint32_t){return &savedCrateFlag;}
void __fastcall brokenControl(void*,void*,int slot){check(slot==5,"original broken-state script changed native flag slot");++nativeBrokenControlCalls;}
int __fastcall otherWorldHit(void*,void*,const Vec3*,void*){return worldHazard;}
void __fastcall securityCookie(uint32_t){}
uintptr_t callback(uint32_t target){switch(target){
    case 0x4014c4:case 0x40163b:return reinterpret_cast<uintptr_t>(fragment);
    case 0x40adc1:return reinterpret_cast<uintptr_t>(effect);
    case 0x40721b:return reinterpret_cast<uintptr_t>(itemSet);
    case 0x405c8b:return reinterpret_cast<uintptr_t>(otherWorldHit);
    case 0x40718f:return reinterpret_cast<uintptr_t>(health);
    case 0x406415:return reinterpret_cast<uintptr_t>(sound);
    case 0xa6cd9a:return reinterpret_cast<uintptr_t>(securityCookie);
    case 0x40eac0:return reinterpret_cast<uintptr_t>(savedCrate);
    case 0x40bdde:return reinterpret_cast<uintptr_t>(brokenControl);
    default:throw std::runtime_error("unknown retail external call");
}}
class OriginalBox {
    uint8_t* code_{};uint8_t* setter_{};uint8_t* script_{};uint32_t cookie_{},globalSlot_{};float limit_{};std::array<uint8_t,0x5040> global_{};
public:
    OriginalBox(){
        std::memcpy(&limit_,&boxfixture::DistanceLimitBits,4);
        code_=static_cast<uint8_t*>(VirtualAlloc(nullptr,sizeof(boxfixture::Code),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        check(code_!=nullptr,"cannot allocate private x86 retail oracle");std::memcpy(code_,boxfixture::Code,sizeof(boxfixture::Code));
        for(auto p:boxfixture::Absolute){
            uintptr_t relocated=p.target>=boxfixture::OriginalStart&&p.target<boxfixture::OriginalEnd
                ?reinterpret_cast<uintptr_t>(code_)+p.target-boxfixture::OriginalStart
                :p.target==0xc35e10?reinterpret_cast<uintptr_t>(&cookie_):reinterpret_cast<uintptr_t>(&limit_);
            std::memcpy(code_+p.offset,&relocated,4);
        }
        for(auto p:boxfixture::Calls)if(p.target<boxfixture::OriginalStart||p.target>=boxfixture::OriginalEnd){
            int32_t displacement=static_cast<int32_t>(callback(p.target)-reinterpret_cast<uintptr_t>(code_+p.offset+5));
            std::memcpy(code_+p.offset+1,&displacement,4);
        }
        DWORD old{};check(VirtualProtect(code_,sizeof(boxfixture::Code),PAGE_EXECUTE_READ,&old)!=0,"cannot make oracle executable");
        check(FlushInstructionCache(GetCurrentProcess(),code_,sizeof(boxfixture::Code))!=0,"cannot flush oracle cache");
        setter_=static_cast<uint8_t*>(VirtualAlloc(nullptr,sizeof(boxfixture::DamageSetterCode),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        check(setter_!=nullptr,"cannot allocate original pending-hit setter");
        std::memcpy(setter_,boxfixture::DamageSetterCode,sizeof(boxfixture::DamageSetterCode));
        check(VirtualProtect(setter_,sizeof(boxfixture::DamageSetterCode),PAGE_EXECUTE_READ,&old)!=0,"cannot make original setter executable");
        check(FlushInstructionCache(GetCurrentProcess(),setter_,sizeof(boxfixture::DamageSetterCode))!=0,"cannot flush original setter cache");
        // Only IDs16/18 below are allowed. The retail setter's invalid-weapon
        // diagnostic branch is retained but never reached or linked to a game.
        setGuestDamageSetterFixture(reinterpret_cast<uintptr_t>(setter_));
        globalSlot_=uint32_t(reinterpret_cast<uintptr_t>(global_.data()));field(global_.data(),0x4fac,uint16_t(0x100));
        script_=static_cast<uint8_t*>(VirtualAlloc(nullptr,sizeof(boxfixture::BrokenScriptCode),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        check(script_!=nullptr,"cannot allocate original broken-state script");std::memcpy(script_,boxfixture::BrokenScriptCode,sizeof(boxfixture::BrokenScriptCode));
        for(auto p:boxfixture::BrokenScriptAbsolute){check(p.target==0xc06f3c,"unknown broken script global");const auto pointer=reinterpret_cast<uintptr_t>(&globalSlot_);std::memcpy(script_+p.offset,&pointer,4);}
        for(auto p:boxfixture::BrokenScriptCalls){const int32_t relative=static_cast<int32_t>(callback(p.target)-reinterpret_cast<uintptr_t>(script_+p.offset+5));std::memcpy(script_+p.offset+1,&relative,4);}
        check(VirtualProtect(script_,sizeof(boxfixture::BrokenScriptCode),PAGE_EXECUTE_READ,&old)!=0,"cannot protect actual broken script");
        check(FlushInstructionCache(GetCurrentProcess(),script_,sizeof(boxfixture::BrokenScriptCode))!=0,"cannot flush actual broken script");
    }
    ~OriginalBox(){setGuestDamageSetterFixture(0);if(script_)VirtualFree(script_,0,MEM_RELEASE);if(setter_)VirtualFree(setter_,0,MEM_RELEASE);if(code_)VirtualFree(code_,0,MEM_RELEASE);}
    void move(uint8_t* actor){reinterpret_cast<void(__fastcall*)(void*,void*)>(code_+boxfixture::ConsumerOffset)(actor,nullptr);
        if(field<uint16_t>(actor,0xfc)==0x101)reinterpret_cast<void(__cdecl*)(void*)>(script_)(actor);}
    uintptr_t dropReturn()const{return reinterpret_cast<uintptr_t>(code_+0x2ea);}
    void productionDrop(bool use){
        DWORD old{};check(VirtualProtect(code_,sizeof(boxfixture::Code),PAGE_READWRITE,&old)!=0,"cannot patch owned drop fixture");
        const auto to=use?guestItemSetFixture():reinterpret_cast<uintptr_t>(itemSet);
        const int32_t displacement=static_cast<int32_t>(to-reinterpret_cast<uintptr_t>(code_+0x2ea));
        std::memcpy(code_+0x2e6,&displacement,4);
        check(VirtualProtect(code_,sizeof(boxfixture::Code),PAGE_EXECUTE_READ,&old)!=0,"cannot protect owned drop fixture");
        check(FlushInstructionCache(GetCurrentProcess(),code_,sizeof(boxfixture::Code))!=0,"cannot flush owned drop fixture");
    }
};
OriginalBox* scopedOriginal{};
void __fastcall originalCrateMove(void* actor,void*){scopedOriginal->move(static_cast<uint8_t*>(actor));}
void reset(){healthCalls=dropCalls=fragments=sounds=effects=0;nativeDamage=appliedDamage=0;itemId=itemCount=0;itemPosition=0;ledger.clear();simulateOldLedger=false;worldHazard=0;savedCrateFlag=0;nativeBrokenControlCalls=0;}
void setup(std::array<uint8_t,0x660>& actor,std::array<uint8_t,0x40>& first,std::array<uint8_t,0x40>& second,uint8_t weapon){
    actor={};first={};second={};field(actor.data(),4,uint32_t(3));field(actor.data(),0xf8,uint32_t(17));field(actor.data(),0x100,uint8_t(0x53));
    field(actor.data(),0x101,uint8_t(4));field(actor.data(),0x324,int16_t(1000));field(actor.data(),0x326,int16_t(1000));
    field(actor.data(),0x32c,uint8_t(1));field(actor.data(),0x32d,uint8_t(10));field(actor.data(),0x32e,weapon);
    field(actor.data(),0x330,Vec3{0,1620,0});field(actor.data(),0x33c,float(0));field(actor.data(),0x340,uint32_t(0x10000));
    field(actor.data(),0x62c,uint32_t(4));field(actor.data(),0x630,uint32_t(10));field(actor.data(),0x64c,uint8_t(0));
    field(actor.data(),0x638,reinterpret_cast<uintptr_t>(first.data()));field(actor.data(),0x63c,reinterpret_cast<uintptr_t>(second.data()));
    first[0x2a]=second[0x2a]=0xff;
}
void fullControllerRoute(OriginalBox& original){
    std::array<uint8_t,0x5040> global{};std::array<uint8_t,0x400> player{};
    std::array<uint8_t,0x660> actor{};std::array<uint8_t,0x40> first{},second{};uint64_t keys[2]{};
    field(global.data(),0x20,uint8_t(3));field(global.data(),0x4fac,uint16_t(0x100));
    field(global.data(),0x4fb4,int16_t(100));field(global.data(),0x4fb6,int16_t(100));
    field(global.data(),0x134,float(70));field(global.data(),0x70,float(1.f/60.f));
    field(player.data(),0,uint32_t(0x123400));field(player.data(),4,uint32_t(1));
    field(player.data(),0xf8,uint32_t(765));field(player.data(),0x2ce,uint16_t(0x31f));
    uint32_t globalSlot=uint32_t(reinterpret_cast<uintptr_t>(global.data())),playerSlot=uint32_t(reinterpret_cast<uintptr_t>(player.data()));
    GameAccess game;game.initializeReadFixture(reinterpret_cast<uintptr_t>(&globalSlot),reinterpret_cast<uintptr_t>(&playerSlot));
    game.keyOn_=reinterpret_cast<uintptr_t>(keys);game.keyTrigger_=game.keyOn_+8;
    check(game.sample().canReplace(),"full crate route requires a real validated native actor/global fixture");
    unsigned scenario{};
    for(uint32_t hitFlags:{0U,uint32_t(HitProjectile),uint32_t(HitExplosion),0U}){
        const bool activePart=scenario++!=3;
        reset();setup(actor,first,second,16);actor[0x32c]=actor[0x32d]=0;
        field(actor.data(),0,uint32_t(0x123400));field(actor.data(),0x94,Vec3{2000,0,0});field(actor.data(),0x110,Vec3{2000,0,0});
        field(actor.data(),0x2c0,float(700));field(actor.data(),0x2c8,float(500));
        // Small wooden-box native body/part dimensions from the bounded live
        // original actor probe. Exact part flags/radius/height are preserved.
        const float matrix[3][4]{{1,0,0,2000},{0,1,0,0},{0,0,1,0}};
        std::memcpy(actor.data()+0xc,matrix,sizeof(matrix));
        NativeHitShape shape{};shape.flags=activePart?9:0;shape.radius=150;shape.height=350;shape.extent=325;
        field(actor.data(),0x344,shape);
        uint32_t manager[]{0x123400,uint32_t(reinterpret_cast<uintptr_t>(actor.data())),1,uint32_t(actor.size())};
        GuestController guest;guest.game_=&game;guest.installed_=true;guest.enemyManager_=reinterpret_cast<uintptr_t>(manager);
        GuestMotion motion{};motion.epoch=7;motion.flags=MotionReady|MotionGrounded;motion.tick=1;motion.health=motion.maxHealth=20;motion.fov=70;
        guest.setActive(true,7,1000,BridgeMode::Minecraft);check(guest.updateMotion(motion),"crate full route fresh source rejected");
        guest.beforeSchedule();const auto exported=guest.takeEnemies();
        check(exported.size()==1&&exported[0].id==18&&(exported[0].flags&ActorBreakable),"actual manager export did not publish alive native wooden crate");
        check(std::abs(exported[0].minX+.7f)<1e-6&&std::abs(exported[0].maxY-.5f)<1e-6&&std::abs(exported[0].maxZ-.7f)<1e-6,"crate source pick omitted its actual native movement footprint");
        if(activePart&&!hitFlags){
            GuestController pausedVehicle;pausedVehicle.game_=&game;pausedVehicle.installed_=true;pausedVehicle.enemyManager_=reinterpret_cast<uintptr_t>(manager);
            auto vehicleMotion=motion;vehicleMotion.flags|=MotionCameraReady;
            pausedVehicle.setActive(true,7,1000,BridgeMode::BeamNG);check(pausedVehicle.updateMotion(vehicleMotion),"preserved vehicle packet scope setup failed");
            pausedVehicle.afterSchedule();const auto prior=pausedVehicle.takeEnemies();
            check(prior.size()==1&&std::abs(prior[0].minX+.15f)<1e-6&&std::abs(prior[0].maxY-.35f)<1e-6&&std::abs(prior[0].maxZ-.325f)<1e-6,"Minecraft crate pick correction changed paused BeamNG packet semantics");
            pausedVehicle.restore();
        }
        GuestMelee attack{};attack.epoch=7;attack.targetId=18;attack.eventId=1;attack.damage=1;attack.flags=hitFlags;
        attack.attackY=1.62;attack.directionX=1;attack.maxReach=3;
        if(hitFlags==HitProjectile){attack.attackX=1.8;attack.attackY=.25;attack.maxReach=1;}
        if(hitFlags==HitExplosion){attack.attackX=0;attack.attackY=.25;attack.maxReach=8;}
        if(!hitFlags){
            check(guest.setBlocks(7,{{.9f,-1,-1,1.1f,3,1}}),"crate wall protection setup failed");
            check(!guest.applyMelee(attack),"body pick footprint let Minecraft punch through placed wall");
            check(guest.setBlocks(7,{}),"crate wall protection cleanup failed");
        }
        check(guest.applyMelee(attack),"full controller rejected real in-range Minecraft crate hit");
        check(!guest.applyMelee(attack),"replayed source event queued a second crate hit");
        uint32_t stackBefore{},stackAfter{};__asm mov stackBefore,esp
        guest.beforeSchedule();__asm mov stackAfter,esp
        check(stackBefore==stackAfter,"actual full scheduler/retail pending-hit setter ABI unbalanced");
        if(!activePart){
            check(!actor[0x32c]&&!actor[0x32d],"native crate with disabled damage part acquired fabricated pending hit");
            original.move(actor.data());
            check(field<int16_t>(actor.data(),0x324)==1000&&!healthCalls&&!dropCalls,"native crate with no active part was forced broken through HP fallback");
            guest.restore();continue;
        }
        check(actor[0x32c]==1&&actor[0x32d]==10&&actor[0x32e]==(hitFlags==HitExplosion?18:16),"actual native setter failed to arm original crate reaction");
        check(field<uint32_t>(actor.data(),0x340)==reinterpret_cast<uintptr_t>(actor.data()+0x344),"full controller fabricated rather than selected real active native damage part");
        check(!guest.damageLedger_.pending(7,18,GetTickCount64()),"crate acquired scaled creature damage ledger in full production controller");
        check(field<int16_t>(actor.data(),0x324)==1000&&!healthCalls&&!dropCalls,"scheduler directly changed crate HP or destruction state");
        original.move(actor.data());
        check(healthCalls==1&&nativeDamage>=1000&&appliedDamage==nativeDamage&&field<int16_t>(actor.data(),0x324)==0,"full export/attack/pending-hit route failed original native one-hit break");
        check(dropCalls==1&&fragments==1&&effects==1&&field<uint16_t>(actor.data(),0xfc)==0x101&&actor[0xfe]==1,"full route did not retain original drop/fragments/broken script");
        guest.restore();
    }
}
void allVariantLootRoutes(OriginalBox& original){
    std::array<uint8_t,0x5040> global{};std::array<uint8_t,0x400> player{};
    std::array<uint8_t,0x660> actor{};std::array<uint8_t,0x40> first{},second{};uint64_t keys[2]{};
    field(global.data(),0x20,uint8_t(3));field(global.data(),0x4fac,uint16_t(0x100));
    field(global.data(),0x4fb4,int16_t(100));field(global.data(),0x4fb6,int16_t(100));
    field(global.data(),0x134,float(70));field(global.data(),0x70,float(1.f/60.f));
    field(player.data(),0,uint32_t(0x123400));field(player.data(),4,uint32_t(1));
    field(player.data(),0xf8,uint32_t(765));field(player.data(),0x2ce,uint16_t(0x31f));
    uint32_t globalSlot=uint32_t(reinterpret_cast<uintptr_t>(global.data())),playerSlot=uint32_t(reinterpret_cast<uintptr_t>(player.data()));
    GameAccess game;game.initializeReadFixture(reinterpret_cast<uintptr_t>(&globalSlot),reinterpret_cast<uintptr_t>(&playerSlot));
    game.keyOn_=reinterpret_cast<uintptr_t>(keys);game.keyTrigger_=game.keyOn_+8;
    const float matrix[3][4]{{0,0,1,2000},{0,1,0,0},{-1,0,0,0}};
    for(uint8_t subtype=1;subtype<=7;++subtype)for(unsigned scenario=0;scenario<8;++scenario){
        reset();setup(actor,first,second,16);actor[0x101]=subtype;actor[0x32c]=actor[0x32d]=0;
        field(actor.data(),0,uint32_t(0x123400));field(actor.data(),0x94,Vec3{2000,0,0});field(actor.data(),0x110,Vec3{2000,0,0});
        field(actor.data(),0x2c0,float(700));field(actor.data(),0x2c8,float(500));std::memcpy(actor.data()+0xc,matrix,sizeof(matrix));
        const auto dimensions=boxfixture::BodyExtents[subtype-1];field(actor.data(),0x61c,Vec3{dimensions[0],dimensions[1],dimensions[2]});
        NativeHitShape shape{};shape.flags=9;shape.radius=dimensions[0]*.5f+50;shape.height=dimensions[1];shape.extent=dimensions[2]*.5f+50;field(actor.data(),0x344,shape);
        // Registered SAT data is the bounded real SDK layout. It is larger
        // than the narrow weapon part and remains an original solid obstacle.
        std::array<uint8_t,0xc0> sat{};std::array<Vec3,8> vertices{};size_t index{};
        for(float x:{-dimensions[0],dimensions[0]})for(float y:{0.f,dimensions[1]})for(float z:{-dimensions[2],dimensions[2]})vertices[index++]={x,y,z};
        field(sat.data(),4,uint32_t(1));field(sat.data(),0xc,reinterpret_cast<uintptr_t>(vertices.data()));field(sat.data(),0x1c,uint16_t(8));sat[0x2a]=4;std::memcpy(sat.data()+0x60,matrix,sizeof(matrix));
        if(subtype==3||subtype==5||subtype==7)field(actor.data(),0x638,reinterpret_cast<uintptr_t>(sat.data()));
        const bool empty=scenario!=1&&scenario!=2;
        if(empty)field(actor.data(),0x62c,uint32_t(UINT32_MAX));
        if(scenario==2)field(actor.data(),0x62c,uint32_t(42)); // Protected progression/key item.
        field(global.data(),0x4f9a,uint8_t(18));field(global.data(),0x54,scenario==5?uint32_t(NativeBonusGameMask):0U);
        if(scenario==6)field(actor.data(),0x634,uint16_t(1));
        uint32_t manager[]{0x123400,uint32_t(reinterpret_cast<uintptr_t>(actor.data())),1,uint32_t(actor.size())};
        GuestController guest;guest.game_=&game;guest.installed_=true;guest.enemyManager_=reinterpret_cast<uintptr_t>(manager);guest.loot_.initialize(0x12345678);
        const auto mode=scenario==3?BridgeMode::Native:scenario==4?BridgeMode::BeamNG:BridgeMode::Minecraft;
        GuestMotion motion{};motion.epoch=7;motion.flags=MotionReady|MotionGrounded|MotionCameraReady;motion.tick=1;motion.health=motion.maxHealth=20;motion.fov=70;
        guest.setActive(mode!=BridgeMode::Native,7,1000,mode);
        if(mode!=BridgeMode::Native)check(guest.updateMotion(motion),"seven-variant source setup rejected");
        if(mode==BridgeMode::Minecraft){
            guest.beforeSchedule();const auto exported=guest.takeEnemies();check(exported.size()==1,"original box variant omitted from production actor export");
            const float minimumX=-std::max(700.f,dimensions[2])/1000,minimumZ=-std::max(700.f,dimensions[0])/1000;
            check(std::abs(exported[0].minX-minimumX)<1e-6&&std::abs(exported[0].minZ-minimumZ)<1e-6&&std::abs(exported[0].maxY-std::max(500.f,dimensions[1])/1000)<1e-6,"rotated original full-body/SAT extents omitted or doubled");
            GuestMelee hit{};hit.epoch=7;hit.targetId=18;hit.eventId=1;hit.damage=1;hit.attackY=1.62;hit.directionX=1;hit.maxReach=3;
            check(guest.setBlocks(7,{{.2f,-1,-2,.3f,3,2}}),"seven-variant wall fixture setup failed");
            check(!guest.applyMelee(hit),"enlarged crate pick let independent nearer wall be punched through");check(guest.setBlocks(7,{}),"wall fixture cleanup failed");
            if(scenario!=7){check(guest.applyMelee(hit),"original large crate body rejected reachable punch");guest.beforeSchedule();
                check(actor[0x32c]&1,"seven-variant original pending-hit setter not selected");}
        }else{actor[0x32c]=1;actor[0x32d]=10;}
        scopedOriginal=&original;original.productionDrop(true);
        if(scenario==7)worldHazard=1;
        setGuestCrateLootFixture(reinterpret_cast<uintptr_t>(originalCrateMove),reinterpret_cast<uintptr_t>(itemSet),original.dropReturn(),&guest);
        uint32_t stackBefore{},stackAfter{};__asm mov stackBefore,esp
        runGuestCrateMoveFixture(actor.data());__asm mov stackAfter,esp
        check(stackBefore==stackAfter,"retail consumer and observed move/drop fastcall/cdecl ABI unbalanced");
        check(field<int16_t>(actor.data(),0x324)==0&&field<uint16_t>(actor.data(),0xfc)==0x101&&actor[0xfe]==1,"original subtype did not complete actual native break and native FE script phase");
        check(savedCrateFlag==1&&nativeBrokenControlCalls==1&&(field<uint16_t>(actor.data(),0x2ce)&0x300)==0,"original native persistent-save/control/collision script lost");
        const bool reward=scenario==0||scenario==1||scenario==7;auto loot=guest.takeLoot();
        check(loot.size()==size_t(reward),"empty/converted/protected/non-Minecraft/bonus crate reward scope incorrect");
        if(reward)check(loot[0].source==2&&loot[0].tier==5&&loot[0].epoch==7&&loot[0].x==2,"completed variant reward lost actual chapter6-1/world/epoch");
        check(dropCalls==unsigned(scenario==2),"converted crate duplicated native pickup or protected key pickup lost");
        runGuestCrateMoveFixture(actor.data());check(guest.takeLoot().empty(),"duplicate native frame awarded a second crate reward");
        setGuestCrateLootFixture(0,0,0,nullptr);original.productionDrop(false);guest.restore();
    }
}
}
int main(){try{
    OriginalBox original;std::array<uint8_t,0x660> actor{};std::array<uint8_t,0x40> first{},second{};
    // Regression oracle: the former generic ledger incorrectly made one native
    // knife hit leave 950 HP, preventing the native destruction branch.
    reset();setup(actor,first,second,16);simulateOldLedger=true;
    check(ledger.arm(7,18,1,reinterpret_cast<uintptr_t>(actor.data()),{16,0x10000,{0,1620,0}},50,100),"old ledger regression setup failed");
    original.move(actor.data());check(nativeDamage==1000&&appliedDamage==50&&field<int16_t>(actor.data(),0x324)==950&&dropCalls==0,"retail oracle did not reproduce scaled crate health bug");
    // A real pending knife is also the current native reaction identity used
    // by genuine Minecraft melee and projectile hits. Explosion is ID18.
    for(uint8_t weapon:{uint8_t(16),uint8_t(18)}){
        reset();setup(actor,first,second,weapon);
        check(ledger.arm(7,18,1,reinterpret_cast<uintptr_t>(actor.data()),{weapon,0x10000,{0,1620,0}},50,100),"foreign creature ledger contamination setup failed");
        uint32_t before{},after{};__asm mov before,esp
        original.move(actor.data());__asm mov after,esp
        check(before==after,"actual retail thiscall/cdecl stack unbalanced");
        check(healthCalls==1&&nativeDamage>=1000&&appliedDamage==nativeDamage,"crate changed original native one-hit damage");
        check(field<int16_t>(actor.data(),0x324)==0,"actual crate destruction did not clear HP");
        check((field<uint32_t>(actor.data(),4)&2)==0,"actual crate destruction did not clear visible/native active flag");
        check(field<uint16_t>(actor.data(),0xfc)==0x101&&actor[0xfe]==1,"actual crate destruction did not schedule original broken-state routine and phase");
        check(first[0x2a]==0xfb&&second[0x2a]==0xfb,"actual native collision registrations remained active after break");
        check(fragments==1&&effects==1,"original destruction effect/fragment services were not called");
        check(dropCalls==1&&itemId==4&&itemCount==10&&itemPosition==reinterpret_cast<uintptr_t>(actor.data()+0x94),"actual native ItemSet route did not receive original drop exactly once");
        original.move(actor.data());check(dropCalls==1&&healthCalls==1,"cleared original pending damage replayed destruction/drop");
    }
    for(uint8_t weapon:{uint8_t(0x14),uint8_t(0x16),uint8_t(0x17),uint8_t(0x2a),uint8_t(0xe),uint8_t(0x19),uint8_t(0x1f),uint8_t(0x20)}){
        reset();setup(actor,first,second,weapon);original.move(actor.data());
        check(!healthCalls&&!dropCalls&&field<int16_t>(actor.data(),0x324)==1000,"retail weapon immunity was overridden");
        check(actor[0x32c]==0,"original consumer did not retire immune pending hit");
    }
    reset();setup(actor,first,second,16);actor[0x32c]=0;original.move(actor.data());check(!healthCalls&&!dropCalls,"no pending native hit broke a crate");
    check(!nativeBreakableKind(0x58)&&!nativeBreakableKind(0x10)&&nativeBreakableKind(0x53),"break route included scripted barrel or creature");
    fullControllerRoute(original);
    allVariantLootRoutes(original);
    std::printf("PASS: %u actual x86 production-manager export, body pick bounds, accepted/replayed/wall-protected Minecraft hit, scheduler, original pending-hit setter and retail box consumer/destruction checks; original damage/fragments/collision/drop/routine/ABI and inactive-part immunity. This is component evidence, not live RE4 or Minecraft gameplay.\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
