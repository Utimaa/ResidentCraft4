// The original x86 instructions/tables are privately extracted from the owned,
// hash-pinned executable. These tests run no game and touch no saved world.
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cmath>
#define private public
#include "../native/guest_controller.hpp"
#undef private
#include "../build/native-special-combat-fixture-private.hpp"
#include <windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace re4craft;
namespace {
unsigned checks{},sounds{},effects{},deaths{},controls{},notifications{};
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
template<class T>T field(const uint8_t* p,size_t at){T v{};std::memcpy(&v,p+at,sizeof(v));return v;}
template<class T>void field(uint8_t* p,size_t at,T v){std::memcpy(p+at,&v,sizeof(v));}
int __cdecl noHazard(){return 0;}
int __cdecl rejectProtectedWeapon(uint32_t){return 1;}
void __cdecl sound(){++sounds;}
void __cdecl effect(){++effects;}
void __cdecl death(){++deaths;}
void __cdecl notify(int kind){check(kind==9,"trap changed native explosion notification ABI");++notifications;}
int __fastcall timer(void*,void*,int){return 1;}
int __fastcall timerNoArgument(void*,void*){return 1;}
void __fastcall control(void*,void*,int slot){check(slot==5,"Gigante death changed native control flag");++controls;}
int __cdecl unexpected(){check(false,"fixture reached unscoped original engine service");return 0;}

class OriginalCombat {
    struct Block {uint32_t start;uint8_t* code;size_t size;};
    std::vector<Block> blocks_;
    std::array<uint8_t,sizeof(specialfixture::PowerTables)> tables_{};
    std::array<specialfixture::Constant,sizeof(specialfixture::Constants)/sizeof(specialfixture::Constants[0])> constants_{};
    uint32_t globalSlot_{},playerSlot_{},sse_{1};
public:
    std::array<uint8_t,0x8500> global{};
    std::array<uint8_t,0x900> player{};
    uintptr_t at(uint32_t address)const{for(const auto& b:blocks_)if(address>=b.start&&address<b.start+b.size)return reinterpret_cast<uintptr_t>(b.code)+address-b.start;return 0;}
    uintptr_t external(uint32_t address){
        if(auto p=at(address))return p;
        if(address>=0xc001b8&&address<0xc010cc)return reinterpret_cast<uintptr_t>(tables_.data())+address-0xc001b8;
        for(auto& c:constants_)if(address==c.address)return reinterpret_cast<uintptr_t>(&c.bits);
        switch(address){
        case 0xc06f3c:return reinterpret_cast<uintptr_t>(&globalSlot_);
        case 0xc57054:return reinterpret_cast<uintptr_t>(&playerSlot_);
        case 0x170dce0:return reinterpret_cast<uintptr_t>(&sse_);
        case 0x40ce14:return guestWeaponPowerFixture();
        case 0x4a9310:case 0x4aa630:return reinterpret_cast<uintptr_t>(noHazard);
        case 0x406415:return reinterpret_cast<uintptr_t>(sound);
        case 0x40adc1:return reinterpret_cast<uintptr_t>(effect);
        case 0x40c0c2:case 0x40de8b:case 0x40eb88:return reinterpret_cast<uintptr_t>(death);
        case 0x407e9b:return reinterpret_cast<uintptr_t>(notify);
        case 0x408341:return reinterpret_cast<uintptr_t>(timer);
        case 0x4078bf:return reinterpret_cast<uintptr_t>(timerNoArgument);
        case 0x40bdde:return reinterpret_cast<uintptr_t>(control);
        case 0x4014fb:case 0x40173a:case 0x40ce5f:return reinterpret_cast<uintptr_t>(unexpected);
        default:throw std::runtime_error("unrecognized original relocation");
        }
    }
    template<size_t N>void allocate(uint32_t start,const uint8_t (&bytes)[N]){
        auto code=static_cast<uint8_t*>(VirtualAlloc(nullptr,N,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
        check(code!=nullptr,"cannot allocate original x86 combat oracle");std::memcpy(code,bytes,N);blocks_.push_back({start,code,N});
    }
    template<class Calls,class Absolute>void relocate(uint32_t start,const Calls& calls,const Absolute& absolute){
        auto code=reinterpret_cast<uint8_t*>(at(start));
        for(auto p:absolute){auto to=external(p.target);std::memcpy(code+p.offset,&to,4);}
        for(auto p:calls){int32_t to=static_cast<int32_t>(external(p.target)-reinterpret_cast<uintptr_t>(code+p.offset+5));std::memcpy(code+p.offset+1,&to,4);}
    }
    OriginalCombat(){
        std::memcpy(tables_.data(),specialfixture::PowerTables,tables_.size());std::copy(std::begin(specialfixture::Constants),std::end(specialfixture::Constants),constants_.begin());
        globalSlot_=uint32_t(reinterpret_cast<uintptr_t>(global.data()));playerSlot_=uint32_t(reinterpret_cast<uintptr_t>(player.data()));
        field(global.data(),0x847c,uint8_t(2));field(global.data(),0x70,1.f);
#define ALLOC(Name) allocate(specialfixture::Name##Start,specialfixture::Name##Code)
        ALLOC(Trap);ALLOC(GiganteCalc);ALLOC(Gigante);ALLOC(VerdugoCalc);ALLOC(SalazarCalc);ALLOC(Power);ALLOC(Convert);ALLOC(Setter);
#undef ALLOC
#define RELOC(Name) relocate(specialfixture::Name##Start,specialfixture::Name##Calls,specialfixture::Name##Absolute)
        RELOC(Trap);RELOC(GiganteCalc);RELOC(Gigante);RELOC(VerdugoCalc);RELOC(SalazarCalc);RELOC(Power);RELOC(Convert);RELOC(Setter);
#undef RELOC
        for(auto b:blocks_){DWORD old{};check(VirtualProtect(b.code,b.size,PAGE_EXECUTE_READ,&old)!=0,"cannot protect original x86 oracle");check(FlushInstructionCache(GetCurrentProcess(),b.code,b.size)!=0,"cannot flush original x86 oracle");}
        setGuestDamageSetterFixture(at(specialfixture::SetterStart));setGuestWeaponPowerFixture(at(specialfixture::PowerStart),nullptr);
    }
    ~OriginalCombat(){setGuestDamageSetterFixture(0);setGuestWeaponPowerFixture(0,nullptr);for(auto b:blocks_)VirtualFree(b.code,0,MEM_RELEASE);}
    int rawPower(void* actor,uint32_t weapon,int weak=0){return reinterpret_cast<int(__cdecl*)(void*,uint32_t,int)>(at(specialfixture::PowerStart))(actor,weapon,weak);}
    void consume(uint32_t start,uint8_t* actor){auto code=at(start);uint32_t before{},after{};
        __asm {
            mov before,esp
            mov eax,actor
            call code
            mov after,esp
        }
        check(before==after,"original EAX actor consumer unbalanced x86 stack");}
    int calculate(uint32_t start,uint8_t* actor){auto code=at(start);int result{};uint32_t before{},after{};
        __asm {
            push esi
            mov before,esp
            mov esi,actor
            call code
            mov result,eax
            mov after,esp
            pop esi
        }
        check(before==after,"original ESI actor calculator unbalanced x86 stack");return result;}
};

struct Scene {
    std::array<uint8_t,0x5040> global{};
    std::array<uint8_t,0x400> player{};
    std::array<uint8_t,0x1000> actor{};
    std::array<uint8_t,63*0x1d8> parts{};
    uint64_t keys[2]{};
    uint32_t globalSlot{},playerSlot{},manager[4]{};
    GameAccess game;GuestController guest;
    Scene(OriginalCombat& original,uint8_t kind,uint8_t subtype=0,bool activePart=true,int hp=1000,int16_t part=0){
        field(global.data(),0x20,uint8_t(3));field(global.data(),0x4fac,uint16_t(0x100));
        field(global.data(),0x4fb4,int16_t(100));field(global.data(),0x4fb6,int16_t(100));field(global.data(),0x134,70.f);field(global.data(),0x70,1.f/60);
        field(player.data(),0,uint32_t(0x123400));field(player.data(),4,uint32_t(1));field(player.data(),0xf8,uint32_t(765));field(player.data(),0x2ce,uint16_t(0x31f));
        globalSlot=uint32_t(reinterpret_cast<uintptr_t>(global.data()));playerSlot=uint32_t(reinterpret_cast<uintptr_t>(player.data()));
        game.initializeReadFixture(reinterpret_cast<uintptr_t>(&globalSlot),reinterpret_cast<uintptr_t>(&playerSlot));game.keyOn_=reinterpret_cast<uintptr_t>(keys);game.keyTrigger_=game.keyOn_+8;
        check(game.sample().canReplace(),"special combat fixture native state invalid");
        field(actor.data(),0,uint32_t(0x123400));field(actor.data(),4,uint32_t(0x2001));field(actor.data(),0xf8,uint32_t(17));
        field(actor.data(),0x100,kind);field(actor.data(),0x101,subtype);field(actor.data(),0x102,uint8_t(63));field(actor.data(),0xf4,reinterpret_cast<uintptr_t>(parts.data()));
        field(actor.data(),0x324,int16_t(hp));field(actor.data(),0x326,int16_t(hp));field(actor.data(),0x94,Vec3{2000,0,0});field(actor.data(),0x110,Vec3{2000,0,0});
        // Bosses use animated YARARE_INFO even when their humanoid atari is
        // absent. No fixture invents fallback character dimensions.
        field(actor.data(),0x2c0,std::numeric_limits<float>::quiet_NaN());
        const float matrix[3][4]{{1,0,0,2000},{0,1,0,0},{0,0,1,0}};std::memcpy(actor.data()+0xc,matrix,sizeof(matrix));
        for(size_t n=0;n<63;++n)std::memcpy(parts.data()+n*0x1d8+0xc,matrix,sizeof(matrix));
        NativeHitShape shape{};shape.flags=activePart?9:0;shape.radius=400;shape.height=2000;shape.extent=400;shape.part=part;field(actor.data(),0x344,shape);
        field(actor.data(),0xa38,int32_t(5000));
        manager[0]=0x123400;manager[1]=uint32_t(reinterpret_cast<uintptr_t>(actor.data()));manager[2]=1;manager[3]=uint32_t(actor.size());
        guest.game_=&game;guest.installed_=true;guest.enemyManager_=reinterpret_cast<uintptr_t>(manager);
        GuestMotion motion{};motion.epoch=7;motion.flags=MotionReady|MotionGrounded;motion.tick=1;motion.health=motion.maxHealth=20;motion.fov=70;
        guest.setActive(true,7,1000,BridgeMode::Minecraft);check(guest.updateMotion(motion),"special combat source setup rejected");
        setGuestWeaponPowerFixture(original.at(specialfixture::PowerStart),&guest);
    }
    ~Scene(){guest.restore();setGuestWeaponPowerFixture(0,nullptr);}
    bool queue(uint32_t flags=0,float amount=4){
        guest.beforeSchedule();auto exported=guest.takeEnemies();if(exported.empty())return false;
        check(exported.size()==1&&exported[0].id==18,"native special actor manager export invalid");
        GuestMelee hit{};hit.epoch=7;hit.targetId=18;hit.eventId=1;hit.flags=flags;hit.damage=amount;hit.attackY=1.62;hit.directionX=1;hit.maxReach=3;
        if(flags&HitProjectile){hit.attackX=1.8;hit.maxReach=1;}
        if(!guest.applyMelee(hit))return false;check(!guest.applyMelee(hit),"replayed special combat event accepted");guest.beforeSchedule();return true;
    }
};

void parserContracts(OriginalCombat& original){
    for(uint8_t kind:{uint8_t(0x2b),uint8_t(0x2c),uint8_t(0x38)}){
        Scene scene(original,kind);Enemy packet{};
        check(sampleGuestActorFixture(reinterpret_cast<uintptr_t>(scene.actor.data()),true,packet),"boss with original animated hit geometry was omitted");
        check(packet.flags&ActorHostile,"boss lost hostile classification");
        check(std::abs(packet.minX+.4f)<1e-6&&std::abs(packet.maxY-2.f)<1e-6,"boss animated hit bounds changed");
        check(!sampleGuestActorFixture(reinterpret_cast<uintptr_t>(scene.actor.data()),false,packet),"Minecraft boss parser changed retained guest route");
        field(scene.actor.data(),0x344+0x24,uint16_t(0));check(!sampleGuestActorFixture(reinterpret_cast<uintptr_t>(scene.actor.data()),true,packet),"boss without native atari or active shapes acquired invented bounds");
    }
    for(uint8_t subtype=0;subtype<5;++subtype){
        Scene scene(original,0x2a,subtype,true,0);Enemy packet{};const auto address=reinterpret_cast<uintptr_t>(scene.actor.data());
        const bool expected=subtype==1||subtype==2;
        check(sampleGuestActorFixture(address,true,packet)==expected,"trap export accepted bear trap or omitted armed zero-HP mine");
        check(!sampleGuestActorFixture(address,false,packet),"mine route leaked into retained guest mode");
        if(expected){check((packet.flags&ActorBreakable)&&!(packet.flags&ActorHostile)&&packet.health>0,"mine source proxy has no selectable interaction health");
            field(scene.actor.data(),0xfc,uint32_t(0x701));check(!sampleGuestActorFixture(address,true,packet),"already detonated trap exported again");
            field(scene.actor.data(),0xfc,uint32_t(0));field(scene.actor.data(),0x368,uint16_t(0));check(!sampleGuestActorFixture(address,true,packet),"unarmed trap with inactive original part exported");}
    }
}

void trapRoutes(OriginalCombat& original){
    for(uint8_t subtype:{uint8_t(1),uint8_t(2)})for(uint32_t flags:{0U,uint32_t(HitProjectile),uint32_t(HitExplosion)}){
        Scene scene(original,0x2a,subtype,true,0);sounds=effects=deaths=controls=notifications=0;
        check(scene.queue(flags),"production scheduler rejected active original trap hit");
        check(scene.actor[0x32c]==1&&scene.actor[0x32e]==guestNativeDamageWeaponForFlags(flags),"trap did not receive exact original pending-hit setter");
        check(field<uint32_t>(scene.actor.data(),0x340)==reinterpret_cast<uintptr_t>(scene.actor.data()+0x344),"trap pending hit did not retain actual native shape");
        check(!scene.guest.damageLedger_.pending(7,18,GetTickCount64()),"native explosive trap acquired creature scaling ledger");
        check(field<uint32_t>(scene.actor.data(),0xfc)==0,"scheduler directly changed trap destruction state");
        original.consume(specialfixture::TrapStart,scene.actor.data());
        check(!scene.actor[0x32c]&&field<uint32_t>(scene.actor.data(),0xfc)==0x701&&field<int16_t>(scene.actor.data(),0x324)==0,"actual native trap consumer failed detonation");
        check(deaths==1&&notifications==1&&sounds==1,"original trap detonation effect/sound/notification path omitted");
        const auto previous=deaths;original.consume(specialfixture::TrapStart,scene.actor.data());check(deaths==previous,"trap pending hit detonated twice");
    }
    for(uint8_t excluded:{uint8_t(0x14),uint8_t(0x16),uint8_t(0x17),uint8_t(0x2a),uint8_t(0x0e)}){
        Scene scene(original,0x2a,1,true,0);scene.actor[0x32c]=1;scene.actor[0x32e]=excluded;deaths=0;
        original.consume(specialfixture::TrapStart,scene.actor.data());check(!deaths&&!field<uint32_t>(scene.actor.data(),0xfc),"original trap immunity was bypassed");
    }
}

void bossRoutes(OriginalCombat& original){
    for(uint32_t flags:{0U,uint32_t(HitProjectile),uint32_t(HitFire),uint32_t(HitExplosion)})for(int16_t part:{int16_t(0),int16_t(0x3f)}){
        Scene scene(original,0x2b,0,true,1000,part);effects=sounds=deaths=controls=0;
        check(scene.queue(flags),"production scheduler omitted actual Gigante combat hit");
        check(scene.actor[0x32c]&&scene.actor[0x32e]==guestNativeDamageWeaponForFlags(flags),"boss pending hit used wrong native weapon");
        check(field<int16_t>(scene.actor.data(),0x324)==1000&&field<int32_t>(scene.actor.data(),0xa38)==5000,"production scheduler bypassed boss consumer with direct HP/stagger write");
        NativeDamageIdentity id{scene.actor[0x32e],field<uint32_t>(scene.actor.data(),0x340),field<Vec3>(scene.actor.data(),0x330)};int selected{};
        check(scene.guest.damageLedger_.consume(7,18,reinterpret_cast<uintptr_t>(scene.actor.data()),id,GetTickCount64(),selected)==DamageClaim::Unrelated,"boss raw-power ledger could scale native LifeDown twice");
        check(original.rawPower(scene.actor.data(),scene.actor[0x32e])>0,"fixture selected unexpectedly immune native weapon");
        check(original.calculate(specialfixture::GiganteCalcStart,scene.actor.data())==200,"real Gigante calculator did not consume mapped Minecraft damage");
        check(original.calculate(specialfixture::GiganteCalcStart,scene.actor.data())==200,"native repeated meter/HP power query lost owned Minecraft damage");
        original.consume(specialfixture::GiganteStart,scene.actor.data());
        check(field<int32_t>(scene.actor.data(),0xa38)==4800&&field<int32_t>(scene.actor.data(),0xa3c)==400,"original Gigante consumer omitted its native stagger meters");
        check(field<int16_t>(scene.actor.data(),0x324)==(part==0x3f?600:1000),"Gigante weak-point versus body HP contract bypassed");
        check(!scene.actor[0x32c]&&effects>0&&sounds>0,"original Gigante reaction effects omitted");
        const auto meter=field<int32_t>(scene.actor.data(),0xa38);original.consume(specialfixture::GiganteStart,scene.actor.data());check(field<int32_t>(scene.actor.data(),0xa38)==meter,"cleared native boss hit applied twice");
    }
    {Scene scene(original,0x2b,0,true,300,0x3f);deaths=controls=0;check(scene.queue(),"Gigante lethal hit queue rejected");original.consume(specialfixture::GiganteStart,scene.actor.data());check(field<uint32_t>(scene.actor.data(),0xfc)==3&&deaths==3&&controls==1,"actual Gigante death phase/services not entered");}
    {Scene scene(original,0x2b,1,true,1000,5);check(scene.queue(),"Gigante subtype/part hit queue rejected");check(original.calculate(specialfixture::GiganteCalcStart,scene.actor.data())==76,"native Gigante part5 1.5x and subtype quarter-plus-one modifiers lost");}
    {Scene scene(original,0x2c);check(scene.queue(HitProjectile),"Verdugo hit queue rejected");check(original.calculate(specialfixture::VerdugoCalcStart,scene.actor.data())==200,"real Verdugo damage calculator omitted raw-power route");field(scene.actor.data(),0x408,uint32_t(0x800));check(original.calculate(specialfixture::VerdugoCalcStart,scene.actor.data())==600,"native frozen Verdugo 3x damage modifier lost");check(original.rawPower(scene.actor.data(),0x17)==0,"original Verdugo ignition immunity fixture invalid");scene.actor[0x32e]=0x17;check(original.calculate(specialfixture::VerdugoCalcStart,scene.actor.data())==0,"original zero-damage Verdugo immunity overridden");}
    {Scene scene(original,0x38);check(scene.queue(HitProjectile),"Salazar hit queue rejected");check(original.calculate(specialfixture::SalazarCalcStart,scene.actor.data())==200,"real Salazar damage calculator omitted raw-power route");scene.actor[0x32e]=0x17;check(original.calculate(specialfixture::SalazarCalcStart,scene.actor.data())==0,"original zero-damage Salazar immunity overridden");}
    {Scene scene(original,0x2b,0,false);check(!scene.queue(),"boss without live native hit part exported/queued");check(field<int16_t>(scene.actor.data(),0x324)==1000,"boss without native shape received direct HP fallback");}
    {Scene scene(original,0x2b,0,false);field(scene.actor.data(),0x2c0,400.f);field(scene.actor.data(),0x2c8,2000.f);
        check(scene.queue(),"boss movement-body export missing before unavailable native-part case");
        check(!scene.actor[0x32c]&&field<int16_t>(scene.actor.data(),0x324)==1000,"queued boss hit without active native damage part used direct HP fallback");
        check(!scene.guest.damageLedger_.pending(7,18,GetTickCount64()),"boss with no accepted native reaction acquired damage ledger");}
    {Scene scene(original,0x2b);field(scene.actor.data(),0x368,uint16_t(0x19));scene.guest.partWeaponFilter_=reinterpret_cast<uintptr_t>(rejectProtectedWeapon);
        check(scene.queue(),"protected native boss part export missing");
        check(!scene.actor[0x32c]&&field<int16_t>(scene.actor.data(),0x324)==1000,"native protected boss part immunity was replaced by direct HP fallback");}
}

void powerOwnership(OriginalCombat& original){
    // Fresh queued hit ownership is checked at the real hook boundary; the
    // exact original power routine remains the fallback for every mismatch.
    Scene scene(original,0x2b);check(scene.queue(HitProjectile),"power ownership queued hit rejected");
    auto actor=scene.actor.data();using Power=int(__cdecl*)(void*,uint32_t,int);auto hook=reinterpret_cast<Power>(guestWeaponPowerFixture());
    const auto weapon=uint32_t(actor[0x32e]);const auto raw=original.rawPower(actor,weapon);
    check(raw>0&&raw!=200,"ownership fixture requires distinct positive original/Minecraft powers");
    check(scene.guest.interceptWeaponPower(actor,weapon,0)==0,"owned zero-damage native immunity overridden");
    check(scene.guest.damageLedger_.pending(7,18,GetTickCount64()),"immune power query retired pending damaging reaction");
    check(hook(actor,weapon,0)==200,"exact original power hook did not own fresh queued attack");
    const auto guid=field<uint32_t>(actor,0xf8);field(actor,0xf8,guid+1);check(hook(actor,weapon,0)==raw,"recycled actor GUID inherited old power claim");field(actor,0xf8,guid);
    const auto source=field<Vec3>(actor,0x330);field(actor,0x330,Vec3{1,2,3});check(hook(actor,weapon,0)==raw,"unrelated native damage source inherited Minecraft power");field(actor,0x330,source);
    const auto shape=field<uint32_t>(actor,0x340);field(actor,0x340,shape+0x34);check(hook(actor,weapon,0)==raw,"different native body part inherited old power");field(actor,0x340,shape);
    check(hook(actor,0x10,0)==original.rawPower(actor,0x10),"different native weapon inherited queued power");
    scene.manager[2]=0;check(hook(actor,weapon,0)==raw,"removed actor retained stale power claim");scene.manager[2]=1;
    scene.guest.damageLedger_.expire(GetTickCount64()+301);check(hook(actor,weapon,0)==raw,"expired power claim survived original consumer window");
    // Ganado must keep the existing LifeDown ownership route even while the
    // specialised lookup is installed, preserving one-time damage semantics.
    field(actor,0x100,uint8_t(0x11));NativeDamageIdentity id{weapon,shape,source};
    check(scene.guest.damageLedger_.arm(7,18,2,reinterpret_cast<uintptr_t>(actor),id,200,GetTickCount64()),"retained Ganado ledger setup rejected");
    check(hook(actor,weapon,0)==original.rawPower(actor,weapon),"specialised power hook scaled retained Ganado path");
    int selected{};check(scene.guest.damageLedger_.consume(7,18,reinterpret_cast<uintptr_t>(actor),id,GetTickCount64(),selected)==DamageClaim::Apply&&selected==200,"specialised power hook consumed Ganado LifeDown ledger");
}

void highDamageNativeDomain(OriginalCombat& original){
    for(uint32_t flags:{0U,uint32_t(HitProjectile),uint32_t(HitFire),uint32_t(HitExplosion)})for(float damage:{400.f,1024.f})for(int hp:{1,2,1000,30000,32766,32767}){
        Scene scene(original,0x2b,0,true,hp,0x3f);deaths=controls=0;
        check(scene.queue(flags,damage),"high-damage Gigante hit queue rejected");
        check(field<int16_t>(scene.actor.data(),0x324)==hp,"high-damage scheduler bypassed original Gigante consumer");
        check(original.calculate(specialfixture::GiganteCalcStart,scene.actor.data())==16383,"exposed Gigante mapped power exceeded proven signed16/2 bound");
        original.consume(specialfixture::GiganteStart,scene.actor.data());
        check(field<int16_t>(scene.actor.data(),0x324)==hp-32766,"large native Gigante weak-point damage wrapped or increased HP");
        const bool lethal=hp<=32766;
        check(field<uint32_t>(scene.actor.data(),0xfc)==(lethal?3U:0U)&&deaths==(lethal?3U:0U)&&controls==(lethal?1U:0U),"high-damage actual Gigante consumer changed original death threshold/phase");
    }
    for(uint8_t subtype:{uint8_t(1),uint8_t(3)})for(uint32_t flags:{0U,uint32_t(HitProjectile),uint32_t(HitFire),uint32_t(HitExplosion)}){
        Scene scene(original,0x2b,subtype,true,10000,0x3f);deaths=controls=0;
        check(scene.queue(flags,1024),"high-damage Gigante subtype hit queue rejected");
        check(original.calculate(specialfixture::GiganteCalcStart,scene.actor.data())==8192,"safe original Gigante subtype quarter-plus-one power was unnecessarily reduced");
        original.consume(specialfixture::GiganteStart,scene.actor.data());
        check(field<int16_t>(scene.actor.data(),0x324)==-6384&&field<uint32_t>(scene.actor.data(),0xfc)==3&&deaths==3&&controls==1,"actual high-damage Gigante subtype death changed");
    }
    for(uint32_t flags:{0U,uint32_t(HitProjectile),uint32_t(HitFire),uint32_t(HitExplosion)}){
        {Scene scene(original,0x2c);check(scene.queue(flags,1024),"high-damage Verdugo queue rejected");
            check(original.calculate(specialfixture::VerdugoCalcStart,scene.actor.data())==32767,"safe unfrozen Verdugo power unnecessarily capped");
            field(scene.actor.data(),0x408,uint32_t(0x800));check(original.calculate(specialfixture::VerdugoCalcStart,scene.actor.data())==98301,"original frozen Verdugo3x high damage changed");}
        {Scene scene(original,0x38);check(scene.queue(flags,1024),"high-damage Salazar queue rejected");
            check(original.calculate(specialfixture::SalazarCalcStart,scene.actor.data())==32767,"safe Salazar power unnecessarily capped");}
    }
    {Scene scene(original,0x2b,0,true,32767,0x3f);check(scene.queue(0,1024),"exhaustive Gigante native HP-domain setup rejected");
        auto actor=scene.actor.data();NativeDamageIdentity id{actor[0x32e],field<uint32_t>(actor,0x340),field<Vec3>(actor,0x330)};
        // Exercise every positive signed16 HP through the original consumer,
        // using a fresh owned maximum-power claim and native hit each time.
        for(int hp=1;hp<=32767;++hp){
            const auto now=GetTickCount64();scene.guest.lastActiveTime_=scene.guest.lastMotionTime_=now;
            scene.guest.damageLedger_.clear();check(scene.guest.damageLedger_.arm(7,18,1,reinterpret_cast<uintptr_t>(actor),id,32767,now,DamageConsumer::WeaponPower),"exhaustive native HP-domain claim rejected");
            field(actor,0x324,int16_t(hp));field(actor,0xfc,uint32_t(0));field(actor,0xa38,int32_t(5000));field(actor,0xa3c,int32_t(0));actor[0x32c]=1;
            original.consume(specialfixture::GiganteStart,actor);
            check(field<int16_t>(actor,0x324)==hp-32766&&field<uint32_t>(actor,0xfc)==(hp<=32766?3U:0U),"original Gigante HP-domain subtraction overflowed or bypassed native death");
        }
    }
    {Scene scene(original,0x2b);auto actor=scene.actor.data();actor[0x32e]=1;field(actor,0x340,uint32_t(1));field(actor,0x330,Vec3{0,1620,0});
        NativeDamageIdentity invalid{1,1,{0,1620,0}};
        check(scene.guest.damageLedger_.arm(7,18,1,reinterpret_cast<uintptr_t>(actor),invalid,32767,GetTickCount64(),DamageConsumer::WeaponPower),"invalid pending-part guard claim setup rejected");
        using Power=int(__cdecl*)(void*,uint32_t,int);auto hook=reinterpret_cast<Power>(guestWeaponPowerFixture());
        check(hook(actor,1,0)==original.rawPower(actor,1),"unreadable fresh Gigante part guard fabricated mapped power instead of original fallback");
    }
}
}
int main(){try{OriginalCombat original;parserContracts(original);trapRoutes(original);bossRoutes(original);powerOwnership(original);highDamageNativeDomain(original);std::printf("PASS: %u actual x86 trap/boss parser, setter, detonation, native stagger/weak-point/death and power-calculator checks; no live gameplay claim.\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,e.what());return 1;}}
