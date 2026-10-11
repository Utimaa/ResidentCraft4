#include "guest_controller.hpp"
#include "vehicle_grab_guard.hpp"
#include "leon_protection.hpp"
#include "hook_support.hpp"
#include "native_hostility.hpp"
#include "vendor/minhook/include/MinHook.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <intrin.h>

namespace re4craft { namespace {
using Schedule=void(__fastcall*)(void*,void*);
using ModelFn=void(__fastcall*)(void*,void*);
using LifeDown=int(__cdecl*)(void*,int,int,uint32_t);
using EnemyAttack=void(__cdecl*)(void*,void*,void*,void*);
using PlayerAttackHit=int(__cdecl*)(void*,void*,void*);
using DamageSet=void(__fastcall*)(void*,void*,uint32_t,uint32_t,uint32_t,const Vec3*,float,void*);
using MeleeDispatch=int(__cdecl*)(void*,const Vec3*,const Vec3*,uint32_t,uint32_t,float);
using AtariAdjust=int(__fastcall*)(void*,void*,void*,uint32_t);
using CollisionTranslate=void(__cdecl*)(void*,const Vec3*);
using DropItem=void(__cdecl*)(void*);
using ItemSet=int(__cdecl*)(const Vec3*,uint32_t,uint32_t,int,int,int,int);
DropItem originalDropItem{};ItemSet originalItemSet{};ModelFn originalCrateMove{};
uintptr_t explicitDropReturn{},randomDropReturn{},crateDropReturn{},attachedDropReturn{},attachedDropReturn2{};
thread_local uintptr_t lootSourceActor{};
struct CrateLootScope {uintptr_t actor{};bool nativeDrop{};};
thread_local CrateLootScope crateLootScope;
Schedule originalSchedule{};ModelFn originalPlayerMove{},originalMatrix{};
AtariAdjust originalAtari{},originalAtariSpecial{};
LifeDown originalLife{};EnemyAttack originalAttack{};GuestController* controller{};
NativeHostility hostileClasses;
PlayerAttackHit originalPlayerAttackHit{};uintptr_t guardedHitReturn{};
DamageSet originalDamageSet{};uintptr_t guardedDamageSetReturn{};
struct ReactionScope {uintptr_t selectedModel{};bool selected{};};
thread_local ReactionScope reactionScope;
thread_local bool playerCollisionOwned{};thread_local uint16_t playerCollisionFlags{};thread_local uintptr_t playerCollisionAddress{};
thread_local ScopedVehicleGrabProtection vehicleGrabScope;
thread_local ScopedVehicleGrabProtection leonGrabScope;
constexpr float Radians=3.14159265358979323846f/180.f;
enum class OwnHook {Matrix,Player,Enemies,ScheduleCapture,EnemyCollision};
struct OwnHookBin {uint64_t calls{},samples{};int64_t ticks{},maximum{};};
struct OwnHookWindow {int64_t frequency{},start{};OwnHookBin bins[5]{};unsigned logs{};};
thread_local OwnHookWindow ownHookWindow;
class OwnHookTimer {
    LogFn log_{};size_t kind_{};int64_t start_{};bool sampled_{};
public:
    OwnHookTimer(LogFn log,OwnHook kind):log_(log),kind_(static_cast<size_t>(kind)){
        if(!log_)return;LARGE_INTEGER now{};QueryPerformanceCounter(&now);start_=now.QuadPart;
        if(!ownHookWindow.frequency){LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);ownHookWindow.frequency=frequency.QuadPart;ownHookWindow.start=start_;}
    }
    void stateSampled(){sampled_=true;}
    ~OwnHookTimer(){
        if(!log_||!ownHookWindow.frequency)return;LARGE_INTEGER now{};QueryPerformanceCounter(&now);int64_t cost=now.QuadPart-start_;
        auto& bin=ownHookWindow.bins[kind_];++bin.calls;if(sampled_)++bin.samples;bin.ticks+=cost;bin.maximum=std::max(bin.maximum,cost);
        int64_t elapsed=now.QuadPart-ownHookWindow.start;if(elapsed<ownHookWindow.frequency*5)return;
        if(ownHookWindow.logs++<60){
            double seconds=double(elapsed)/ownHookWindow.frequency;char message[840]{};size_t used{};
            used=std::snprintf(message,sizeof(message),"Guest hook performance (bridge only, %.2f s):",seconds);
            const char* names[]{"matrix","player","enemyRefresh","scheduleCapture","enemyCollision"};
            for(size_t i=0;i<5&&used<sizeof(message);++i){const auto& b=ownHookWindow.bins[i];int wrote=std::snprintf(message+used,sizeof(message)-used," %s %.0f calls/s %.0f samples/s %.3f ms/s avg %.2f us max %.2f us;",names[i],b.calls/seconds,b.samples/seconds,(double(b.ticks)*1000/ownHookWindow.frequency)/seconds,b.calls?double(b.ticks)*1e6/(ownHookWindow.frequency*b.calls):0.,double(b.maximum)*1e6/ownHookWindow.frequency);if(wrote<0)break;used+=static_cast<size_t>(wrote);}
            log_(message);
        }
        for(auto& b:ownHookWindow.bins)b={};ownHookWindow.start=now.QuadPart;
    }
};
void combatLog(LogFn log,const char* message){
    if(!log)return;
    // Hook and render callbacks run on the native game thread. Event-only
    // diagnostics are capped independently on each caller thread; no frame log.
    thread_local uint64_t window{};thread_local unsigned count{},total{};
    if(total>=2048)return;uint64_t now=GetTickCount64();if(now-window>=1000){window=now;count=0;}
    if(count>=16)return;++count;++total;log(message);
}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool bounded(double x){return std::isfinite(x)&&std::abs(x)<100000.;}
float axis(Vec3 p,int a){return a==0?p.x:a==1?p.y:p.z;}
void axis(Vec3& p,int a,float value){if(a==0)p.x=value;else if(a==1)p.y=value;else p.z=value;}
float minimum(const BlockBox& b,int a){return a==0?b.minX:a==1?b.minY:b.minZ;}
float maximum(const BlockBox& b,int a){return a==0?b.maxX:a==1?b.maxY:b.maxZ;}
bool alive(uint32_t flags){return (flags&1)!=0&&(flags&0x600)==0;}
struct Manager {uint32_t vtable,array,count,stride;};
bool readManager(uintptr_t address,Manager& m){return hookdetail::copy(address,&m,sizeof(m))&&m.count>0&&m.count<=512&&m.stride>=0x408&&m.stride<=0x10000&&m.array>=0x10000&&m.count<=((UINT32_MAX-m.array)/m.stride);}
bool member(uintptr_t manager,uintptr_t model){Manager m{};return readManager(manager,m)&&model>=m.array&&(model-m.array)%m.stride==0&&(model-m.array)/m.stride<m.count;}
struct ShapeRecord {uintptr_t address{};NativeActorBounds bounds{};uint16_t flags{};};
struct EnemyData {uintptr_t address{},vtable{};uint32_t flags{},guid{},parts{};uint8_t kind{},subtype{},partCount{},damageFlags{},damageTimer{};uint16_t atariFlags{};int16_t hp{},maxHp{};Vec3 position{},old{},offset{};float yaw{},radius{},height{},matrix[3][4]{};NativeHitShape firstShape{};NativeActorBounds bounds{},crateBodyBounds{};std::array<ShapeRecord,32> shapes{};size_t shapeCount{};};
template<class T> void actorField(const uint8_t* bytes,size_t offset,T& out){std::memcpy(&out,bytes+offset,sizeof(out));}
uintptr_t shapePart(const EnemyData& e,int number){
    if(number==0)return e.address;
    if(number<0||number>e.partCount||!e.parts)return 0;
    // cModel::GetParts65B7C0 takes the zero-based index. BE_FLAG2000
    // owns contiguous cParts storage; other models use nextParts_F4.
    if(e.flags&0x2000)return e.parts+uintptr_t(number-1)*0x1d8;
    uintptr_t at=e.parts;
    for(int n=1;n<number&&at;++n){uint32_t next{};if(!hookdetail::copy(at+0xf4,&next,4)||next==at)return 0;at=next;}
    return at;
}
void readHitBounds(EnemyData& e){
    NativeHitShape shape=e.firstShape;uintptr_t at=e.address+0x344;
    std::array<uintptr_t,64> visited{};size_t count{};
    struct PartMatrix {int part;float matrix[3][4];};std::array<PartMatrix,32> matrices{};size_t matrixCount{};
    while(at&&count<visited.size()){
        if(std::find(visited.begin(),visited.begin()+count,at)!=visited.begin()+count)break;
        visited[count++]=at;
        if(at!=e.address+0x344&&!hookdetail::copy(at,&shape,sizeof(shape)))break;
        if(shape.flags&1){
            const float (*matrix)[4]=shape.part==0?e.matrix:nullptr;
            if(!matrix){
                auto cached=std::find_if(matrices.begin(),matrices.begin()+matrixCount,[&](const PartMatrix& m){return m.part==shape.part;});
                if(cached!=matrices.begin()+matrixCount)matrix=cached->matrix;
                else if(matrixCount<matrices.size()){
                    uintptr_t part=shapePart(e,shape.part);
                    auto& value=matrices[matrixCount];if(part&&hookdetail::copy(part+0xc,value.matrix,sizeof(value.matrix))){value.part=shape.part;matrix=value.matrix;++matrixCount;}
                }
            }
            if(matrix){NativeActorBounds current{};if(appendNativeHitShapeBounds(current,shape,matrix,e.position)){
                if(e.shapeCount<e.shapes.size())e.shapes[e.shapeCount++]={at,current,shape.flags};
                if(!e.bounds.valid)e.bounds=current;
                else for(int a=0;a<3;++a){axis(e.bounds.minimum,a,std::min(axis(e.bounds.minimum,a),axis(current.minimum,a)));axis(e.bounds.maximum,a,std::max(axis(e.bounds.maximum,a),axis(current.maximum,a)));}
            }}
        }
        at=shape.next;
    }
}
void mergeBounds(NativeActorBounds& target,const NativeActorBounds& value){
    if(!value.valid)return;
    if(!target.valid){target=value;return;}
    for(int a=0;a<3;++a){axis(target.minimum,a,std::min(axis(target.minimum,a),axis(value.minimum,a)));axis(target.maximum,a,std::max(axis(target.maximum,a),axis(value.maximum,a)));}
}
void readCrateBodyBounds(EnemyData& e){
    if(!nativeBreakableKind(e.kind)||e.subtype<1||e.subtype>7)return;
    // Retail SetBox stores all seven variants' actual model extents here.
    // Its local-space body check (5C2F50) uses X/Z +/-these full extents,
    // while the weapon parts (5C2AD0) deliberately use half-sized X/Z.
    // Picking only those parts makes a large crate's native solid win the ray.
    Vec3 extents{};
    if(hookdetail::copy(e.address+0x61c,&extents,sizeof(extents))&&finite(extents)
        &&extents.x>0&&extents.y>0&&extents.z>0&&extents.x<=16000&&extents.y<=32000&&extents.z<=16000){
        NativeHitShape body{};body.flags=9;body.radius=extents.x;body.height=extents.y;body.extent=extents.z;
        appendNativeHitShapeBounds(e.crateBodyBounds,body,e.matrix,e.position);
    }
    // Large box variants also own registered native SAT bodies. Read their
    // real transformed vertices, never another prop or guessed dimensions.
    uint32_t bodies[2]{};if(!hookdetail::copy(e.address+0x638,bodies,sizeof(bodies)))return;
    for(size_t n=0;n<2;++n){
        const uintptr_t at=bodies[n];if(!at||(n&&bodies[0]==bodies[1]))continue;
        std::array<uint8_t,0xc0> bytes{},check{};if(!hookdetail::copy(at,bytes.data(),bytes.size()))continue;
        uint32_t flags{},vertices{};uint16_t count{};float matrix[3][4]{};
        actorField(bytes.data(),4,flags);actorField(bytes.data(),0xc,vertices);actorField(bytes.data(),0x1c,count);actorField(bytes.data(),0x60,matrix);
        if((flags&0x201)!=1||!(bytes[0x2a]&4)||vertices<0x10000||count<3||count>256)continue;
        bool matrixValid=true;for(auto& row:matrix)for(float v:row)if(!std::isfinite(v)||std::abs(v)>1e8f)matrixValid=false;
        if(!matrixValid)continue;
        std::array<Vec3,256> points{};if(!hookdetail::copy(vertices,points.data(),count*sizeof(Vec3)))continue;
        NativeActorBounds body{};bool valid=true;
        for(size_t i=0;i<count;++i){const auto p=points[i];
            if(!finite(p)){valid=false;break;}
            Vec3 relative{matrix[0][0]*p.x+matrix[0][1]*p.y+matrix[0][2]*p.z+matrix[0][3]-e.position.x,
                matrix[1][0]*p.x+matrix[1][1]*p.y+matrix[1][2]*p.z+matrix[1][3]-e.position.y,
                matrix[2][0]*p.x+matrix[2][1]*p.y+matrix[2][2]*p.z+matrix[2][3]-e.position.z};
            if(!finite(relative)||std::abs(relative.x)>16000||std::abs(relative.y)>32000||std::abs(relative.z)>16000){valid=false;break;}
            NativeActorBounds vertex{relative,relative,true};mergeBounds(body,vertex);
        }
        // A room unload or registration change invalidates this body.
        if(valid&&hookdetail::copy(at,check.data(),check.size())&&bytes==check)mergeBounds(e.crateBodyBounds,body);
    }
}
bool readEnemy(uintptr_t p,EnemyData& e,bool hitBounds=false){
    // One validated cEm prefix replaces eleven per-field VirtualQuery calls.
    // The exact GUID/HP are still freshly copied before every native write.
    std::array<uint8_t,0x378> bytes{};if(!hookdetail::copy(p,bytes.data(),bytes.size()))return false;
    e.address=p;actorField(bytes.data(),0,e.vtable);actorField(bytes.data(),4,e.flags);actorField(bytes.data(),0x100,e.kind);actorField(bytes.data(),0x101,e.subtype);
    if(!alive(e.flags)||(!nativeEnemyKind(e.kind)&&!nativeBreakableKind(e.kind)))return false;
    actorField(bytes.data(),0xf8,e.guid);actorField(bytes.data(),0x94,e.position);actorField(bytes.data(),0x110,e.old);actorField(bytes.data(),0xa4,e.yaw);
    actorField(bytes.data(),0x324,e.hp);actorField(bytes.data(),0x326,e.maxHp);actorField(bytes.data(),0x2b4,e.offset);actorField(bytes.data(),0x2c0,e.radius);actorField(bytes.data(),0x2c8,e.height);
    actorField(bytes.data(),0x2ce,e.atariFlags);
    actorField(bytes.data(),0x32c,e.damageFlags);actorField(bytes.data(),0x32d,e.damageTimer);
    actorField(bytes.data(),0xf4,e.parts);actorField(bytes.data(),0x102,e.partCount);actorField(bytes.data(),0xc,e.matrix);actorField(bytes.data(),0x344,e.firstShape);
    if(!(e.guid<INT32_MAX&&e.hp>0&&e.maxHp>=e.hp&&finite(e.position)&&finite(e.old)&&finite(e.offset)&&std::isfinite(e.yaw)))return false;
    bool atari=std::isfinite(e.radius)&&std::isfinite(e.height)&&e.radius>0&&e.radius<=16000&&e.height>0&&e.height<=32000;
    if(!atari&&!nativeBreakableKind(e.kind))return false;
    if(hitBounds||(!atari&&nativeBreakableKind(e.kind)))readHitBounds(e);
    // Boxes with no live native hit shape and no native movement bounds are
    // omitted rather than fabricating dimensions or making scripted props hittable.
    return atari||e.bounds.valid;
}
uintptr_t nearestHitShape(const EnemyData& e,Vec3 source,bool protectedWeaponRejected){
    uintptr_t selected{};float nearest=std::numeric_limits<float>::infinity();
    for(size_t i=0;i<e.shapeCount;++i){const auto& shape=e.shapes[i];if(!nativeHitPartAcceptsWeapon(shape.flags,protectedWeaponRejected))continue;float distance{};
        for(int a=0;a<3;++a){float relative=axis(source,a)-axis(e.position,a),d=std::clamp(relative,axis(shape.bounds.minimum,a),axis(shape.bounds.maximum,a))-relative;distance+=d*d;}
        if(distance<nearest){nearest=distance;selected=shape.address;}
    }
    return selected;
}
bool damageIdentity(uintptr_t model,NativeDamageIdentity& info){
    uint8_t weapon{};if(!hookdetail::copy(model+0x32e,&weapon,1))return false;info.weapon=weapon;
    return hookdetail::copy(model+0x330,&info.source,12)&&hookdetail::copy(model+0x340,&info.yarare,4)&&finite(info.source);
}
Enemy packet(const EnemyData& e,float units,bool minecraftCrateBody=false){
    const bool hostile=!nativeBreakableKind(e.kind)&&!hostileClasses.merchantActor(e.vtable)
        &&(knownNativeHostileKind(e.kind)||hostileClasses.commonGanadoActor(e.kind,e.vtable));
    Enemy p{};p.id=e.guid+1;p.flags=ActorAlive|(nativeBreakableKind(e.kind)?ActorBreakable:0U)|(hostile?ActorHostile:0U);p.x=e.position.x/units;p.y=e.position.y/units;p.z=e.position.z/units;
    p.minX=(e.offset.x-e.radius)/units;p.maxX=(e.offset.x+e.radius)/units;p.minY=e.offset.y/units;p.maxY=(e.offset.y+e.height)/units;p.minZ=(e.offset.z-e.radius)/units;p.maxZ=(e.offset.z+e.radius)/units;
    if(e.bounds.valid){
        // A wooden crate's narrow weapon-test part can sit inside its native
        // obstacle footprint. Minecraft must pick the actual crate body before
        // that obstacle; otherwise a punch becomes a terrain-block click and
        // never reaches the original crate consumer. Retain both verified native
        // bounds for this one breakable kind. Damage still selects a real active
        // weapon-compatible part below; no dimension or room geometry is guessed.
        const bool crateBody=minecraftCrateBody&&nativeBreakableKind(e.kind)&&std::isfinite(e.radius)&&std::isfinite(e.height)
            &&e.radius>0&&e.radius<=16000&&e.height>0&&e.height<=32000;
        p.minX=(crateBody?std::min(e.bounds.minimum.x,e.offset.x-e.radius):e.bounds.minimum.x)/units;
        p.minY=(crateBody?std::min(e.bounds.minimum.y,e.offset.y):e.bounds.minimum.y)/units;
        p.minZ=(crateBody?std::min(e.bounds.minimum.z,e.offset.z-e.radius):e.bounds.minimum.z)/units;
        p.maxX=(crateBody?std::max(e.bounds.maximum.x,e.offset.x+e.radius):e.bounds.maximum.x)/units;
        p.maxY=(crateBody?std::max(e.bounds.maximum.y,e.offset.y+e.height):e.bounds.maximum.y)/units;
        p.maxZ=(crateBody?std::max(e.bounds.maximum.z,e.offset.z+e.radius):e.bounds.maximum.z)/units;
    }
    if(minecraftCrateBody&&e.crateBodyBounds.valid){
        p.minX=std::min(p.minX,e.crateBodyBounds.minimum.x/units);p.minY=std::min(p.minY,e.crateBodyBounds.minimum.y/units);p.minZ=std::min(p.minZ,e.crateBodyBounds.minimum.z/units);
        p.maxX=std::max(p.maxX,e.crateBodyBounds.maximum.x/units);p.maxY=std::max(p.maxY,e.crateBodyBounds.maximum.y/units);p.maxZ=std::max(p.maxZ,e.crateBodyBounds.maximum.z/units);
    }
    p.health=e.hp/50.f;p.maxHealth=e.maxHp/50.f;p.yaw=-e.yaw/Radians;p.kind=e.kind;return p;
}
void __fastcall schedule(void* self,void* unused){if(controller)controller->beforeSchedule();originalSchedule(self,unused);if(controller)controller->afterSchedule();}
void __fastcall playerMove(void* self,void* unused){if(controller)controller->beforePlayerMove(self);if(!controller||!controller->suppressPlayerMove())originalPlayerMove(self,unused);if(controller)controller->afterPlayerMove(self);}
void __fastcall matrixUpdate(void* self,void* unused){if(controller)controller->beforeModelMatrix(self);originalMatrix(self,unused);}
int __fastcall atariAdjust(void* self,void* unused,void* actor,uint32_t flags){
    bool clipped=controller&&controller->constrainEnemyAtari(actor);
    int result=originalAtari(self,unused,actor,flags);
    clipped=(controller&&controller->constrainEnemyAtari(actor))||clipped;return clipped?1:result;
}
int __fastcall atariAdjustSpecial(void* self,void* unused,void* actor,uint32_t flags){
    bool clipped=controller&&controller->constrainEnemyAtari(actor);
    int result=originalAtariSpecial(self,unused,actor,flags);
    clipped=(controller&&controller->constrainEnemyAtari(actor))||clipped;return clipped?1:result;
}
int __cdecl lifeDown(void* model,int damage,int randomRange,uint32_t flags){return controller?controller->interceptLife(model,damage,randomRange,flags):originalLife(model,damage,randomRange,flags);}
void __cdecl enemyAttack(void* yarare,void* info,void* from,void* to){if(!controller||!controller->interceptEnemyAttack(yarare,info,from,to))originalAttack(yarare,info,from,to);}
int __cdecl playerAttackHit(void* info,void* from,void* to){
    // Only EmAtkHitCk's verified call owns the direct HP=0 branch. Other
    // callers retain the original hit-check result and native behavior.
    bool guarded=reinterpret_cast<uintptr_t>(_ReturnAddress())==guardedHitReturn;
    int hit=originalPlayerAttackHit(info,from,to);
    if(guarded&&hit&&controller&&controller->interceptEnemyAttack(nullptr,info,from,to))return 0;
    return hit;
}
void __fastcall damageSet(void* self,void* unused,uint32_t flags,uint32_t timer,uint32_t weapon,const Vec3* from,float distance,void* yarare){
    bool guarded=reactionScope.selectedModel&&reinterpret_cast<uintptr_t>(_ReturnAddress())==guardedDamageSetReturn;
    if(guarded&&(reinterpret_cast<uintptr_t>(self)!=reactionScope.selectedModel+0x328||weapon!=16))return;
    originalDamageSet(self,unused,flags,timer,weapon,from,distance,yarare);
    if(guarded)reactionScope.selected=true;
}
void __cdecl dropItem(void* actor){
    auto previous=lootSourceActor;lootSourceActor=reinterpret_cast<uintptr_t>(actor);
    originalDropItem(actor);lootSourceActor=previous;
}
void __fastcall crateMove(void* actor,void* unused){
    // Observe only an actual original box consumer. Original code owns
    // the break, effects, collision unregister and native room bookkeeping.
    if(!controller||!controller->wantsCrateRewards()){originalCrateMove(actor,unused);return;}
    std::array<uint8_t,12> damage{};std::array<uint8_t,0x104> before{};
    uint32_t flags{},guid{};int16_t hp{};uint8_t kind{},subtype{};
    bool candidate=hookdetail::copy(reinterpret_cast<uintptr_t>(actor)+0x324,damage.data(),damage.size());
    if(candidate){actorField(damage.data(),0,hp);candidate=hp>0;}
    if(candidate)candidate=hookdetail::copy(reinterpret_cast<uintptr_t>(actor),before.data(),before.size());
    if(candidate){actorField(before.data(),4,flags);actorField(before.data(),0xf8,guid);actorField(before.data(),0x100,kind);actorField(before.data(),0x101,subtype);
        candidate=alive(flags)&&kind==0x53&&subtype>=1&&subtype<=7&&guid<INT32_MAX;}
    auto previous=crateLootScope;
    if(candidate)crateLootScope={reinterpret_cast<uintptr_t>(actor),false};
    originalCrateMove(actor,unused);
    if(candidate){
        if(!crateLootScope.nativeDrop)controller->interceptCrateBreak(reinterpret_cast<uintptr_t>(actor),guid,subtype);
        crateLootScope=previous;
    }
}
int __cdecl itemSet(const Vec3* position,uint32_t id,uint32_t count,int effect,int saveIndex,int opaque,int other){
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    uintptr_t actor{};uint32_t source{};const Vec3* lootPosition=position;
    if(lootSourceActor&&(caller==explicitDropReturn||caller==randomDropReturn)){actor=lootSourceActor;source=1;}
    else if(lootSourceActor&&(caller==attachedDropReturn||caller==attachedDropReturn2)){
        // Native moving-platform helpers use a local pickup position. The
        // Minecraft drop belongs at this validated actor's real world origin.
        actor=lootSourceActor;source=1;lootPosition=reinterpret_cast<const Vec3*>(actor+0x94);
    }
    else if(caller==crateDropReturn&&reinterpret_cast<uintptr_t>(position)>=0x94){actor=reinterpret_cast<uintptr_t>(position)-0x94;source=2;}
    // Any original pickup (including a protected key/treasure) owns this
    // container's reward. Empty native containers never reach ItemSet.
    if(source==2&&crateLootScope.actor==actor)crateLootScope.nativeDrop=true;
    // Exact callers ignore EAX and resume native actor/room bookkeeping. -1
    // is the allocator's own no-allocation result, not a fabricated object.
    if(actor&&controller&&controller->interceptLoot(actor,source,lootPosition,id,count))return -1;
    return originalItemSet(position,id,count,effect,saveIndex,opaque,other);
}
}
#ifdef RE4CRAFT_ACCESS_FIXTURE
void setGuestDamageSetterFixture(uintptr_t address){originalDamageSet=reinterpret_cast<DamageSet>(address);}
void setGuestCrateLootFixture(uintptr_t move,uintptr_t allocator,uintptr_t returnAddress,GuestController* owner){originalCrateMove=reinterpret_cast<ModelFn>(move);originalItemSet=reinterpret_cast<ItemSet>(allocator);crateDropReturn=returnAddress;controller=owner;}
void runGuestCrateMoveFixture(void* actor){crateMove(actor,nullptr);}
uintptr_t guestItemSetFixture(){return reinterpret_cast<uintptr_t>(itemSet);}
#endif
bool validGuestMotion(const GuestMotion& m,uint32_t epoch,bool vehicleMode){
    return validGuestMotion(m,epoch,vehicleMode?BridgeMode::BeamNG:BridgeMode::Minecraft);
}
bool validGuestMotion(const GuestMotion& m,uint32_t epoch,BridgeMode mode){
    const float velocityLimit=guestMotionVelocityLimit(mode);
    if(velocityLimit<=0)return false;
    if(m.epoch!=epoch||!(m.flags&MotionReady)||(m.flags&~31U)||m.cameraMode>2||m.reserved)return false;
    if(!bounded(m.hostX)||!bounded(m.hostY)||!bounded(m.hostZ)||!bounded(m.cameraX)||!bounded(m.cameraY)||!bounded(m.cameraZ))return false;
    for(float x:{m.velocityX,m.velocityY,m.velocityZ,m.yaw,m.pitch,m.health,m.maxHealth,m.cameraRoll,m.fov})if(!std::isfinite(x))return false;
    return std::abs(m.velocityX)<=velocityLimit&&std::abs(m.velocityY)<=velocityLimit&&std::abs(m.velocityZ)<=velocityLimit&&std::abs(m.yaw)<1e7f&&std::abs(m.pitch)<=90&&m.health>=0&&m.health<=m.maxHealth&&m.maxHealth>0&&m.maxHealth<=1024&&std::abs(m.cameraRoll)<=360&&m.fov>=20&&m.fov<=140;
}
bool validBlockBox(const BlockBox& b){
    for(float x:{b.minX,b.minY,b.minZ,b.maxX,b.maxY,b.maxZ})if(!bounded(x))return false;
    return b.maxX>b.minX&&b.maxY>b.minY&&b.maxZ>b.minZ&&b.maxX-b.minX<=16&&b.maxY-b.minY<=16&&b.maxZ-b.minZ<=16;
}
bool nativeEnemyKind(uint32_t k){return k>0x10&&k<0x4f&&k!=0x18&&k!=0x2a&&k!=0x3b&&k!=0x3d&&k!=0x4e;}
bool appendNativeHitShapeBounds(NativeActorBounds& bounds,const NativeHitShape& shape,const float matrix[3][4],Vec3 actorPosition){
    if(!(shape.flags&1)||!finite(shape.offset)||!finite(actorPosition)||!std::isfinite(shape.radius)||!std::isfinite(shape.height)||!std::isfinite(shape.extent)||shape.radius<0||shape.radius>128000||std::abs(shape.height)>128000||std::abs(shape.extent)>128000)return false;
    for(size_t row=0;row<3;++row)for(size_t col=0;col<4;++col)if(!std::isfinite(matrix[row][col]))return false;
    auto transform=[&](Vec3 p){return Vec3{matrix[0][0]*p.x+matrix[0][1]*p.y+matrix[0][2]*p.z+matrix[0][3]-actorPosition.x,matrix[1][0]*p.x+matrix[1][1]*p.y+matrix[1][2]*p.z+matrix[1][3]-actorPosition.y,matrix[2][0]*p.x+matrix[2][1]*p.y+matrix[2][2]*p.z+matrix[2][3]-actorPosition.z};};
    NativeActorBounds current{};
    if(shape.flags&8){
        // The native box helper5B4C70 checks X +/-radius, Y0..height,
        // Z +/-extent in the translated part-local frame (no half-size guess).
        for(float x:{-shape.radius,shape.radius})for(float y:{0.f,shape.height})for(float z:{-shape.extent,shape.extent}){
            auto p=transform({shape.offset.x+x,shape.offset.y+y,shape.offset.z+z});if(!finite(p))return false;
            if(!current.valid){current.minimum=current.maximum=p;current.valid=true;}
            else for(int a=0;a<3;++a){axis(current.minimum,a,std::min(axis(current.minimum,a),axis(p,a)));axis(current.maximum,a,std::max(axis(current.maximum,a),axis(p,a)));}
        }
    }else{
        // Native5BA1E7 chooses the capsule's local axis from flags2/4,
        // transforms both endpoints, and scales radius by mat_C's Z column.
        Vec3 end=shape.offset;int localAxis=(shape.flags&2)?0:(shape.flags&4)?2:1;axis(end,localAxis,axis(end,localAxis)+shape.height);
        auto a=transform(shape.offset),b=transform(end);
        float radius=shape.radius*std::sqrt(matrix[0][2]*matrix[0][2]+matrix[1][2]*matrix[1][2]+matrix[2][2]*matrix[2][2]);
        if(!finite(a)||!finite(b)||!std::isfinite(radius))return false;
        for(int k=0;k<3;++k){axis(current.minimum,k,std::min(axis(a,k),axis(b,k))-radius);axis(current.maximum,k,std::max(axis(a,k),axis(b,k))+radius);}current.valid=true;
    }
    for(int a=0;a<3;++a)if(std::abs(axis(current.minimum,a))>128000||std::abs(axis(current.maximum,a))>128000)return false;
    if(!bounds.valid)bounds=current;
    else for(int a=0;a<3;++a){axis(bounds.minimum,a,std::min(axis(bounds.minimum,a),axis(current.minimum,a)));axis(bounds.maximum,a,std::max(axis(bounds.maximum,a),axis(current.maximum,a)));}
    return true;
}
bool validEnemyRecord(const Enemy& e){
    if(!e.id||e.id>INT32_MAX||(e.flags&~15U)||e.kind>65535||(e.flags&(ActorBreakable|ActorHostile))==(ActorBreakable|ActorHostile))return false;
    for(double x:{e.x,e.y,e.z})if(!std::isfinite(x)||std::abs(x)>30000000)return false;
    for(float x:{e.minX,e.minY,e.minZ,e.maxX,e.maxY,e.maxZ})if(!std::isfinite(x)||std::abs(x)>128)return false;
    return e.minX<=e.maxX&&e.minY<=e.maxY&&e.minZ<=e.maxZ&&std::isfinite(e.health)&&std::isfinite(e.maxHealth)&&std::isfinite(e.yaw)&&e.health>=0&&e.maxHealth>0&&e.health<=e.maxHealth;
}
bool appendUniqueEnemy(std::vector<Enemy>& records,const Enemy& e){
    if(records.size()>=256||!validEnemyRecord(e)||std::any_of(records.begin(),records.end(),[&](const Enemy& prior){return prior.id==e.id;}))return false;
    records.push_back(e);return true;
}
Vec3 clipEnemyMotion(Vec3 from,Vec3 to,float radius,float height,const std::vector<BlockBox>& boxes){
    if(!finite(from)||!finite(to)||!std::isfinite(radius)||!std::isfinite(height)||radius<=0||height<=0)return from;
    Vec3 at=from;Vec3 delta{to.x-from.x,to.y-from.y,to.z-from.z};
    // Newly placed shapes, or a collider arriving after the native AI move,
    // can start inside the capsule. Resolve the least translation first;
    // a sweep starting from an overlapping interval cannot detect its entry.
    for(unsigned pass=0;pass<8;++pass){
        bool intersecting=false;float nearest=std::numeric_limits<float>::infinity(),shift{};int chosen{};
        float negative[3]{},positive[3]{};
        for(const auto& b:boxes){
            if(!validBlockBox(b))continue;bool overlap=true;
            for(int k=0;k<3;++k){float lo=axis(at,k)-(k==1?0:radius),hi=axis(at,k)+(k==1?height:radius);if(hi<=minimum(b,k)+1e-5f||lo>=maximum(b,k)-1e-5f){overlap=false;break;}}
            if(!overlap)continue;intersecting=true;
            for(int k=0;k<3;++k){
                float lo=axis(at,k)-(k==1?0:radius),hi=axis(at,k)+(k==1?height:radius);
                // Clear the union of all currently intersecting cells. Choosing
                // one cell alone oscillates across adjacent full blocks/stairs.
                negative[k]=std::min(negative[k],minimum(b,k)-hi);positive[k]=std::max(positive[k],maximum(b,k)-lo);
            }
        }
        for(int k:{0,2,1})for(float candidate:{negative[k],positive[k]})if(candidate!=0&&std::abs(candidate)<nearest){nearest=std::abs(candidate);shift=candidate;chosen=k;}
        if(!intersecting)break;if(!std::isfinite(nearest)||nearest>16)return from;
        axis(at,chosen,axis(at,chosen)+shift);
    }
    // Axis sweep catches a fast crossing even if the endpoint is beyond a wall.
    // Vertical first permits stepping/falling onto freshly placed block tops.
    for(int a:{1,0,2}){
        float step=axis(delta,a),lo=axis(at,a)-(a==1?0:radius),hi=axis(at,a)+(a==1?height:radius);
        for(const auto& b:boxes){
            if(!validBlockBox(b))continue;bool overlap=true;
            for(int k=0;k<3;++k)if(k!=a){float low=axis(at,k)-(k==1?0:radius),high=axis(at,k)+(k==1?height:radius);if(high<=minimum(b,k)+1e-5f||low>=maximum(b,k)-1e-5f){overlap=false;break;}}
            if(!overlap)continue;
            if(step>0&&hi<=minimum(b,a)+1e-5f)step=std::min(step,minimum(b,a)-hi);
            else if(step<0&&lo>=maximum(b,a)-1e-5f)step=std::max(step,maximum(b,a)-lo);
        }
        axis(at,a,axis(at,a)+step);
    }
    return at;
}
bool meleeInReach(const GuestMelee& a,const Enemy& e,Vec3 feet){
    if(!a.targetId||a.targetId!=e.id||!a.eventId||(a.flags&~63U)||!std::isfinite(a.damage)||a.damage<=0||a.damage>1024||!std::isfinite(a.knockback)||a.knockback<0||a.knockback>16||!bounded(a.attackX)||!bounded(a.attackY)||!bounded(a.attackZ)||!std::isfinite(a.maxReach)||a.maxReach<=0||a.maxReach>16||!finite(feet))return false;
    for(float x:{a.directionX,a.directionY,a.directionZ})if(!std::isfinite(x)||std::abs(x)>1.001f)return false;
    bool projectile=(a.flags&2)!=0,hazard=(a.flags&8)!=0,explosion=(a.flags&16)!=0,ally=(a.flags&HitAlly)!=0;
    if(ally&&(a.flags!=HitAlly||a.maxReach>3||(e.flags&ActorBreakable)||!(e.flags&ActorHostile)))return false;
    if((hazard&&explosion)||((hazard||explosion)&&(a.flags&3)))return false;
    if((projectile||hazard)&&a.maxReach>1)return false;
    if(!projectile&&!hazard&&!explosion&&a.maxReach>6)return false;
    double dx=a.attackX-feet.x,dy=a.attackY-feet.y,dz=a.attackZ-feet.z;if(dx*dx+dy*dy+dz*dz>((projectile||hazard||explosion||ally)?128.*128.:16.))return false;
    double x=std::clamp(a.attackX,e.x+e.minX,e.x+e.maxX)-a.attackX,y=std::clamp(a.attackY,e.y+e.minY,e.y+e.maxY)-a.attackY,z=std::clamp(a.attackZ,e.z+e.minZ,e.z+e.maxZ)-a.attackZ;
    return x*x+y*y+z*z<=double(a.maxReach)*a.maxReach;
}
bool guestAttackUsesBlockRay(uint32_t flags){return (flags&(8U|16U))==0;}
bool blockedByGuestBlocks(Vec3 from,Vec3 to,const std::vector<BlockBox>& boxes){
    if(!finite(from)||!finite(to))return true;
    for(const auto& b:boxes){
        float enter=0,exit=1;bool hit=true;
        for(int a=0;a<3;++a){float origin=axis(from,a),d=axis(to,a)-origin;
            if(std::abs(d)<1e-7f){if(origin<minimum(b,a)||origin>maximum(b,a)){hit=false;break;}continue;}
            float lo=(minimum(b,a)-origin)/d,hi=(maximum(b,a)-origin)/d;if(lo>hi)std::swap(lo,hi);enter=std::max(enter,lo);exit=std::min(exit,hi);if(enter>exit){hit=false;break;}
        }
        if(hit&&exit>.001f&&enter<.999f)return true;
    }
    return false;
}
bool GuestController::installHooks(GameAccess* game,LogFn log){
    if(installed_)return game_==game;if(!game||!game->initialized()||controller)return false;
    hookdetail::ImageCode code;if(!code.initialize())return false;
    uintptr_t scheduler=code.find("74 ? B9 ? ? ? ? E8 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? E8");
    uintptr_t player=code.find("55 8B EC A1 ? ? ? ? 83 A0 20 50 00 00 FB A1 ? ? ? ? 83 EC 14 53 56 8B F1");
    uintptr_t matrixTail=code.find("83 BE F4 00 00 00 00 74 ? 8B CE E8 ? ? ? ? 8B CE E8 ? ? ? ? 56 8D 8E 64 01 00 00");
    // Verified unique cModel::matUpdate inner signature is exactly +0x3b
    // from its entry in this executable; require its full entry instructions.
    uintptr_t matrix=matrixTail&&code.contains(matrixTail-0x3b,0x3b)?matrixTail-0x3b:0;
    const uint8_t matrixPrologue[]{0x56,0x8b,0xf1,0x57,0x8d,0x86,0xa0,0,0,0,0x50,0x8d,0x7e,0x3c,0x57};uint8_t actual[sizeof(matrixPrologue)]{};
    if(!matrix||!hookdetail::copy(matrix,actual,sizeof(actual))||std::memcmp(actual,matrixPrologue,sizeof(actual)))matrix=0;
    uintptr_t life=code.find("55 8B EC 53 56 57 E8 ? ? ? ? 0F B6 F0 C1 E6 08");
    uintptr_t attack=code.find("55 8B EC 83 EC 20 A1 ? ? ? ? 33 C5 89 45 FC 8B 4D 14");
    uintptr_t hitCaller=code.find("55 8B EC 8B 45 ? 8B 4D ? 53 56 8B 75 ? 57 50 51");
    // The unique upstream EmAtkHitCk entry calls the player geometry check
    // at +0x14 and then conditionally subtracts HP/directly writes HP=0.
    uintptr_t hit=hitCaller?code.callTarget(hitCaller+0x14):0;
    if(hit!=code.find("55 8B EC 83 EC 20 A1 ? ? ? ? 33 C5 89 45 FC A1 ? ? ? ? 66 83 B8 B4 4F 00 00 00 53 8B 5D 08 56 8B 75 0C 57 8B 7D 10"))hit=0;
    uintptr_t knifeAudio=code.find("83 C0 ? 50 6A 03 6A 01 E8");
    uintptr_t knifeHelper=knifeAudio?code.callTarget(knifeAudio+0xb5):0;
    uintptr_t dispatch=knifeHelper?code.callTarget(knifeHelper+0x12b):0;
    if(dispatch!=code.find("55 8B EC 81 EC D8 00 00 00 A1 ? ? ? ? 33 C5 89 45 FC 8B 45 10 53 8B 5D 14 0F B7 D3 56 89 85 38 FF FF FF"))dispatch=0;
    uintptr_t setter=dispatch?code.callTarget(dispatch+0x17f):0;
    if(setter!=code.find("55 8B EC 8A 45 08 D9 EE 0C 01 56 8B F1 8A 4D 0C D9 1E D9 45 18"))setter=0;
    uintptr_t cancel=setter?setter+0xa0:0;
    const uint8_t cancelPrologue[]{0xd9,0xee,0x66,0xc7,0x41,0x04,0x00,0x00,0xd9,0x19,0xc3};uint8_t actualCancel[sizeof(cancelPrologue)]{};
    if(!code.contains(cancel,sizeof(cancelPrologue))||!hookdetail::copy(cancel,actualCancel,sizeof(actualCancel))||std::memcmp(actualCancel,cancelPrologue,sizeof(cancelPrologue)))setter=0;
    // Derive the read-only protected-part weapon gate through the original ray
    // tester. Each offset is verified against this executable, then independently
    // require the gate's unique complete entry rather than using a fixed VA.
    uintptr_t ray=dispatch?code.callTarget(dispatch+0xfa):0;
    uintptr_t shapeTest=ray?code.callTarget(ray+0x470):0;
    uintptr_t partFilter=shapeTest?code.callTarget(shapeTest+0x54):0;
    if(!partFilter||partFilter!=code.find("55 8B EC 0F B7 45 08 48 83 F8 2A 77 ? 0F B6 80 ? ? ? ? FF 24 85 ? ? ? ? B8 01 00 00 00 5D C3"))return false;
    uintptr_t emAlive=code.find("8B 35 ? ? ? ? 85 F6 74 ? 8B C6 8B 76 08 3B 05");
    // Common Ganado move45AE20 builds animated matrices itself and reaches
    // these two native scene adjusters instead of virtual matUpdate65D000.
    // Both are ordinary thiscall int(actor*,flags), return8. Their own delta
    // helper moves the actor and existing animated part matrices together.
    uintptr_t atari=code.find("55 8B EC 51 53 8B 5D 08 0F B7 83 CE 02 00 00 57 8B F9 C7 45 FC 00 00 00 00 A9 00 01 00 00");
    uintptr_t atariSpecial=code.find("55 8B EC 83 EC 44 A1 ? ? ? ? 33 C5 89 45 FC 56 8B 75 08 57 33 FF B8 00 01 00 00 89 4D C0 89 7D C4");
    uintptr_t translate=atariSpecial?code.callTarget(atariSpecial+0xf1):0;
    if(translate!=code.find("55 8B EC 53 8B 5D 08 56 8B B3 F4 00 00 00 57 8B 7D 0C 57 E8 ? ? ? ? D9 EE"))translate=0;
    if(!translate)return false;
    // The common Ganado caller passes this actual SatMgr object to both
    // already-verified adjusters. Optional vehicle shoves use its original
    // normal adjuster with flags0, exactly as this native call does.
    uintptr_t satCaller=code.find("F7 86 B0 04 00 00 00 00 08 00 B9 ? ? ? ? 74 ? 68 10 28 1C 00 56 E8 ? ? ? ? EB ? 6A 00 56 E8");
    uintptr_t vehicleSat=satCaller&&code.callTarget(satCaller+23)==atariSpecial&&code.callTarget(satCaller+33)==atari?hookdetail::operand(satCaller+11):0;
    uint8_t satHeader[0x20]{};if(vehicleSat&&!hookdetail::copy(vehicleSat,satHeader,sizeof(satHeader)))vehicleSat=0;
    std::array<uintptr_t,9> targets{scheduler?code.callTarget(scheduler+17):0,player,matrix,life,attack,hit,setter,atari,atariSpecial};
    for(auto p:targets)if(!p){if(log)log("Guest controller signatures missing/nonunique: native motion/combat takeover disabled.");return false;}
    if(!emAlive)return false;auto aliveSlot=hookdetail::operand(emAlive+2);if(aliveSlot<0x14)return false;enemyManager_=aliveSlot-0x14;
    for(auto p:targets){uint8_t b{};if(!hookdetail::copy(p,&b,1)||b==0xe9)return false;}
    auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    std::array<void*,9> detours{reinterpret_cast<void*>(schedule),reinterpret_cast<void*>(playerMove),reinterpret_cast<void*>(matrixUpdate),reinterpret_cast<void*>(lifeDown),reinterpret_cast<void*>(enemyAttack),reinterpret_cast<void*>(playerAttackHit),reinterpret_cast<void*>(damageSet),reinterpret_cast<void*>(atariAdjust),reinterpret_cast<void*>(atariAdjustSpecial)};
    std::array<void**,9> originals{reinterpret_cast<void**>(&originalSchedule),reinterpret_cast<void**>(&originalPlayerMove),reinterpret_cast<void**>(&originalMatrix),reinterpret_cast<void**>(&originalLife),reinterpret_cast<void**>(&originalAttack),reinterpret_cast<void**>(&originalPlayerAttackHit),reinterpret_cast<void**>(&originalDamageSet),reinterpret_cast<void**>(&originalAtari),reinterpret_cast<void**>(&originalAtariSpecial)};
    guardedHitReturn=hitCaller+0x19;
    guardedDamageSetReturn=dispatch+0x184;
    // Optional classification is immutable before native callbacks start.
    // Failed class discovery only excludes mixed Ganado/Merchant labels from
    // automatic wolf targeting; ordinary motion and player combat still work.
    hostileClasses.initialize(log);
    game_=game;log_=log;controller=this;size_t created{};
    for(;created<targets.size();++created)if(MH_CreateHook(reinterpret_cast<void*>(targets[created]),detours[created],originals[created])!=MH_OK)break;
    if(created<targets.size()){for(size_t i=0;i<created;++i)MH_RemoveHook(reinterpret_cast<void*>(targets[i]));controller=nullptr;return false;}
    size_t enabled{};for(;enabled<targets.size();++enabled)if(MH_EnableHook(reinterpret_cast<void*>(targets[enabled]))!=MH_OK)break;
    if(enabled<targets.size()){for(auto p:targets){MH_DisableHook(reinterpret_cast<void*>(p));MH_RemoveHook(reinterpret_cast<void*>(p));}controller=nullptr;return false;}
    installed_=true;matrixFunction_=matrix;meleeDispatcher_=dispatch;meleeCancel_=cancel;partWeaponFilter_=partFilter;collisionTranslate_=translate;vehicleSatManager_=vehicleSat;
    // Optional loot hooks fail open without disabling already verified combat.
    uintptr_t drop=code.find("55 8B EC 83 EC 18 A1 ? ? ? ? 33 C5 89 45 FC 56 8B 75 08 0F B7 8E DE 03 00 00 B8 FF FF 00 00");
    uintptr_t crate=code.find("83 66 04 FD 33 C0 66 89 86 24 03 00 00 8A 86 4C 06 00 00 53 57 3C FF");
    uintptr_t crateUpdate=code.find("56 8B F1 E8 ? ? ? ? 0F B6 86 FC 00 00 00 81 66 04 FF BF FF FF 8B 0C 85 ? ? ? ? 56 FF D1 A1 ? ? ? ? BA 27 02 00 00");
    uintptr_t allocator=drop?code.callTarget(drop+0x150):0;
    uintptr_t random=drop?code.callTarget(drop+0x169):0;
    uintptr_t attached=drop?code.callTarget(drop+0x102):0;
    uintptr_t attached2=drop?code.callTarget(drop+0x11f):0;
    if(allocator!=code.find("55 8B EC 83 EC 0C 57 6A 0D 6A 01 6A 00 6A 00 68 9C 00 00 00 E8"))allocator=0;
    if(random!=code.find("55 8B EC 51 56 8B 75 08 0F B6 96 00 01 00 00 6A 00 8D 45 08 50 8D 4D FC 51 52 E8"))random=0;
    if(attached!=code.find("55 8B EC 83 EC 18 A1 ? ? ? ? 33 C5 89 45 FC 8B 15 ? ? ? ? 8B 45 08 B9 1B 02 00 00"))attached=0;
    if(attached2!=code.find("55 8B EC 83 EC 18 A1 ? ? ? ? 33 C5 89 45 FC 8B 45 08 8B 0D ? ? ? ? 89 45 E8 0F B7 81 AC 4F 00 00 BA 0F 03 00 00"))attached2=0;
    bool lootVerified=drop&&crate&&crateUpdate&&allocator&&random&&attached&&attached2&&code.callTarget(crateUpdate+3)==crate+0x400&&code.callTarget(random+0x67)==allocator&&code.callTarget(crate+0x2e5)==allocator&&code.callTarget(attached+0xbd)==allocator&&code.callTarget(attached2+0xce)==allocator;
    if(lootVerified){
        bool dropCreated=MH_CreateHook(reinterpret_cast<void*>(drop),reinterpret_cast<void*>(dropItem),reinterpret_cast<void**>(&originalDropItem))==MH_OK;
        bool allocatorCreated=dropCreated&&MH_CreateHook(reinterpret_cast<void*>(allocator),reinterpret_cast<void*>(itemSet),reinterpret_cast<void**>(&originalItemSet))==MH_OK;
        bool crateCreated=allocatorCreated&&MH_CreateHook(reinterpret_cast<void*>(crateUpdate),reinterpret_cast<void*>(crateMove),reinterpret_cast<void**>(&originalCrateMove))==MH_OK;
        explicitDropReturn=drop+0x155;randomDropReturn=random+0x6c;crateDropReturn=crate+0x2ea;
        attachedDropReturn=attached+0xc2;attachedDropReturn2=attached2+0xd3;
        if(crateCreated&&MH_EnableHook(reinterpret_cast<void*>(drop))==MH_OK&&MH_EnableHook(reinterpret_cast<void*>(allocator))==MH_OK&&MH_EnableHook(reinterpret_cast<void*>(crateUpdate))==MH_OK){
            FILETIME time{};GetSystemTimeAsFileTime(&time);LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);
            uint64_t session=(uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;session^=uint64_t(counter.QuadPart)^(uint64_t(GetCurrentProcessId())<<17);loot_.initialize(session?session:1);
            if(log){char line[224]{};std::snprintf(line,sizeof(line),"Native ordinary loot route installed: EmSetDropItem %08X ItemSet %08X crate caller %08X move %08X; seven container variants and empty completed breaks; main-campaign subchapter tiers.",unsigned(drop),unsigned(allocator),unsigned(crateDropReturn),unsigned(crateUpdate));log(line);}
        }else{
            if(dropCreated){MH_DisableHook(reinterpret_cast<void*>(drop));MH_RemoveHook(reinterpret_cast<void*>(drop));}if(allocatorCreated){MH_DisableHook(reinterpret_cast<void*>(allocator));MH_RemoveHook(reinterpret_cast<void*>(allocator));}
            if(crateCreated){MH_DisableHook(reinterpret_cast<void*>(crateUpdate));MH_RemoveHook(reinterpret_cast<void*>(crateUpdate));}
            if(log)log("Native loot hooks unavailable; native item drops retained.");
        }
    }else if(log)log("Native loot signatures missing/altered; native item drops retained.");
    if(log){char s[256]{};std::snprintf(s,sizeof(s),"Guest motion/combat hooks installed: scheduler %08X player %08X matrix %08X health %08X enemyAttack %08X hitCheck %08X manager %08X.",unsigned(targets[0]),unsigned(player),unsigned(matrix),unsigned(life),unsigned(attack),unsigned(hit),unsigned(enemyManager_));log(s);
        std::snprintf(s,sizeof(s),"Minecraft melee/environment reaction route verified: dispatcher %08X damageInfo %08X cancel %08X partWeaponFilter %08X (selected-GUID ledger).",unsigned(dispatch),unsigned(setter),unsigned(cancel),unsigned(partFilter));log(s);
        std::snprintf(s,sizeof(s),"Native enemy placed-block adjustment hooks: normal %08X special %08X matrix-preserving translation %08X; vehicle SatMgr %08X.",unsigned(atari),unsigned(atariSpecial),unsigned(translate),unsigned(vehicleSatManager_));log(s);}return true;
}
void GuestController::setActive(bool active,uint32_t epoch,float units,bool vehicleMode){
    setActive(active,epoch,units,vehicleMode?BridgeMode::BeamNG:BridgeMode::Minecraft);
}
void GuestController::setActive(bool active,uint32_t epoch,float units,BridgeMode mode){
    const bool vehicleMode=mode==BridgeMode::BeamNG;
    std::lock_guard lock(mutex_);lastActiveTime_=GetTickCount64();requestedActive_=active&&installed_&&guestBridgeMode(mode)&&std::isfinite(units)&&units>0&&units<10000;
    vehicleVisualProxy_.clear();
    if(epoch_!=epoch||guestMode_!=mode){epoch_=epoch;motionValid_=false;blocks_.clear();blockRegionValid_=false;damage_.clear();damageDiagnostics_.clear();meleeDiagnostics_.clear();enemies_.clear();enemyAddresses_.clear();pendingMelee_.clear();damageLedger_.clear();vehicleContacts_.clear();vehicleMotionHistory_.clear();lastMeleeId_=0;lastEnemyTime_=0;lastCollisionSample_=0;}
    guestMode_=mode;
    vehicleMode_=vehicleMode;
    units_=units;if(!requestedActive_){pendingMelee_.clear();enemies_.clear();enemyAddresses_.clear();damageLedger_.clear();vehicleContacts_.clear();vehicleMotionHistory_.clear();meleeDiagnostics_.clear();lastEnemyTime_=0;}
}
void GuestController::setVehicleVisualHold(bool held,uint32_t epoch){
    std::lock_guard lock(mutex_);vehicleVisualProxy_.set(held&&installed_&&vehicleMode_&&epoch==epoch_,epoch,GetTickCount64());
}
bool GuestController::updateMotion(const GuestMotion& m){
    std::lock_guard lock(mutex_);if(!validGuestMotion(m,epoch_,guestMode_)||((vehicleMode_||modernGuestMode(guestMode_))&&!(m.flags&MotionCameraReady))||(motionValid_&&m.tick<=motion_.tick))return false;
    motion_=m;motionValid_=true;lastMotionTime_=GetTickCount64();if(vehicleMode_)vehicleMotionHistory_.push(m,lastMotionTime_);return true;
}
bool GuestController::setBlocks(uint32_t epoch,const std::vector<BlockBox>& blocks){
    if(blocks.size()>4096||!std::all_of(blocks.begin(),blocks.end(),validBlockBox))return false;
    BlockBox region{};if(!blocks.empty()){region=blocks.front();for(const auto& b:blocks)for(int a=0;a<3;++a){if(a==0){region.minX=std::min(region.minX,b.minX);region.maxX=std::max(region.maxX,b.maxX);}else if(a==1){region.minY=std::min(region.minY,b.minY);region.maxY=std::max(region.maxY,b.maxY);}else{region.minZ=std::min(region.minZ,b.minZ);region.maxZ=std::max(region.maxZ,b.maxZ);}}}
    bool changed{};{std::lock_guard lock(mutex_);if(epoch!=epoch_||!minecraftGameplayRoutes(guestMode_))return false;changed=blocks_.size()!=blocks.size();blocks_=blocks;blockRegion_=region;blockRegionValid_=!blocks.empty();}
    if(changed){char line[192]{};std::snprintf(line,sizeof(line),"Guest placed-block colliders accepted: epoch=%u count=%u.",epoch,unsigned(blocks.size()));combatLog(log_,line);}return true;
}
bool GuestController::applyMelee(const GuestMelee& attack){
    bool accepted{};const char* reason="state/epoch/dead/queue/event";
    {std::lock_guard lock(mutex_);accepted=[&]{
        if(attack.epoch!=epoch_||!requestedFreshLocked()||(motion_.flags&MotionDead)||motion_.health<=0||pendingMelee_.size()>=64||attack.eventId<=lastMeleeId_||vehicleMode_!=(attack.flags==HitVehicle))return false;
        auto it=std::find_if(enemies_.begin(),enemies_.end(),[&](const Enemy& e){return e.id==attack.targetId;});
        GuestMotion impactMotion{};
        reason="target/range/origin";if(it==enemies_.end()||(vehicleMode_?!vehicleMotionHistory_.match(attack,*it,motion_,GetTickCount64(),impactMotion):!meleeInReach(attack,*it,{float(motion_.hostX),float(motion_.hostY),float(motion_.hostZ)})))return false;
        reason="crate-fire-unsupported";if(nativeBreakableKind(it->kind)&&!nativeBreakableAttack(attack.flags))return false;
        reason="guest-block";if(minecraftGameplayRoutes(guestMode_)&&guestAttackUsesBlockRay(attack.flags)&&blockedByGuestBlocks({float(attack.attackX),float(attack.attackY),float(attack.attackZ)},{float(it->x+(it->minX+it->maxX)*.5),float(it->y+(it->minY+it->maxY)*.5),float(it->z+(it->minZ+it->maxZ)*.5)},blocks_))return false;
        reason="vehicle-contact-rate/replay";if(vehicleMode_&&!vehicleContacts_.accept(epoch_,attack.targetId,attack.eventId,GetTickCount64()))return false;
        lastMeleeId_=attack.eventId;pendingMelee_.push_back({attack,GetTickCount64()+2000,impactMotion});reason="queued";return true;
    }();}
    char line[256]{};std::snprintf(line,sizeof(line),"Guest combat input: epoch=%u event=%u target=%u rawMC=%.3f flags=%u reach=%.3f result=%s.",attack.epoch,attack.eventId,attack.targetId,double(attack.damage),attack.flags,double(attack.maxReach),reason);combatLog(log_,line);
    return accepted;
}
std::vector<HostDamage> GuestController::takeDamage(){
    std::vector<HostDamage> out;std::vector<DamageDiagnostic> diagnostics;
    {std::lock_guard lock(mutex_);out.swap(damage_);diagnostics.swap(damageDiagnostics_);}
    for(const auto& d:diagnostics){char line[256]{};std::snprintf(line,sizeof(line),"Native damage redirected: epoch=%u event=%u rawNative=%.3f rawMC=%.3f kind=%u flags=%u guestHP=%.3f nativeHP=retained (queued for Minecraft).",d.epoch,d.event,double(d.rawNative),double(d.rawMinecraft),d.kind,d.flags,double(d.guestHealth));combatLog(log_,line);}
    return out;
}
std::vector<Enemy> GuestController::takeEnemies()const{std::lock_guard lock(mutex_);return enemies_;}
std::vector<HostLoot> GuestController::takeLoot(){std::lock_guard lock(mutex_);return minecraftGameplayRoutes(guestMode_)?loot_.take():std::vector<HostLoot>{};}
bool GuestController::retryLoot(const HostLoot& loot){std::lock_guard lock(mutex_);return minecraftGameplayRoutes(guestMode_)&&loot_.retry(loot);}
bool GuestController::interceptLoot(uintptr_t actor,uint32_t source,const Vec3* position,uint32_t id,uint32_t count){
    {std::lock_guard lock(mutex_);if(!minecraftGameplayRoutes(guestMode_)||!requestedFreshLocked()||!game_)return false;}
    if(!member(enemyManager_,actor)||reinterpret_cast<uintptr_t>(position)!=actor+0x94)return false;
    uint8_t prefix[0x104]{};if(!hookdetail::copy(actor,prefix,sizeof(prefix)))return false;
    uint8_t kind=prefix[0x100];uint32_t flags{};Vec3 at{};actorField(prefix,4,flags);actorField(prefix,0x94,at);
    if(!(flags&1)||!finite(at)||(source==1?!nativeGanadoKind(kind):source!=2||!nativeBreakableKind(kind)))return false;
    uint16_t itemFlags[2]{};if(!hookdetail::copy(actor+(source==1?0x3e2:0x634),itemFlags,sizeof(itemFlags))||!nativeLootItemEligible(id,count,itemFlags[0],itemFlags[1]))return false;
    auto s=game_->sample();const auto lootContext=game_->lootContext();
    if(!s.canReplace()||!lootContext.valid)return false;
    int tier=nativeLootTier(lootContext.chapter,lootContext.character,lootContext.systemFlags);if(tier<0)return false;
    HostLoot accepted{};bool queued{};
    {std::lock_guard lock(mutex_);if(!minecraftGameplayRoutes(guestMode_)||!activeLocked(s))return false;
        const double x=at.x/units_,y=at.y/units_,z=at.z/units_;double dx=x-motion_.hostX,dy=y-motion_.hostY,dz=z-motion_.hostZ;
        if(dx*dx+dy*dy+dz*dz>128.*128.)return false;
        queued=loot_.append(epoch_,source,uint32_t(tier),x,y,z,accepted);
    }
    if(queued){char line[240]{};std::snprintf(line,sizeof(line),"Native ordinary loot converted: epoch=%u event=%u source=%u chapter=%u tier=%u nativeItem=%u count=%u host=(%.2f,%.2f,%.2f).",accepted.epoch,accepted.eventId,source,unsigned(lootContext.chapter),accepted.tier,id,count,accepted.x,accepted.y,accepted.z);combatLog(log_,line);}return queued;
}
bool GuestController::interceptCrateBreak(uintptr_t actor,uint32_t originalGuid,uint8_t originalSubtype){
    if(crateLootScope.actor!=actor||crateLootScope.nativeDrop)return false;
    {std::lock_guard lock(mutex_);if(!minecraftGameplayRoutes(guestMode_)||!requestedFreshLocked()||!game_)return false;}
    if(originalSubtype<1||originalSubtype>7||!member(enemyManager_,actor))return false;
    std::array<uint8_t,0x638> after{};if(!hookdetail::copy(actor,after.data(),after.size()))return false;
    uint32_t flags{},guid{},item{};int16_t hp{};uint16_t routine{},saveFlags{},scriptFlags{};Vec3 at{};
    actorField(after.data(),4,flags);actorField(after.data(),0xf8,guid);actorField(after.data(),0xfc,routine);
    actorField(after.data(),0x324,hp);actorField(after.data(),0x94,at);actorField(after.data(),0x62c,item);
    actorField(after.data(),0x634,saveFlags);actorField(after.data(),0x636,scriptFlags);
    // FC/FD name the original broken routine; FE is its native script phase,
    // already advanced by the full original move and never reset here.
    // The original destructor completed an empty, ordinary container. Keys,
    // treasures and room-persistent/scripted drops retain original behavior.
    if(!(flags&1)||guid!=originalGuid||after[0x100]!=0x53||after[0x101]!=originalSubtype
        ||hp!=0||routine!=0x101||item!=UINT32_MAX||saveFlags||scriptFlags||!finite(at))return false;
    auto s=game_->sample();const auto context=game_->lootContext();
    if(!s.canReplace()||!context.valid)return false;
    const int tier=nativeLootTier(context.chapter,context.character,context.systemFlags);if(tier<0)return false;
    HostLoot accepted{};bool queued{};
    {std::lock_guard lock(mutex_);if(!minecraftGameplayRoutes(guestMode_)||!activeLocked(s))return false;
        const double x=at.x/units_,y=at.y/units_,z=at.z/units_;const double dx=x-motion_.hostX,dy=y-motion_.hostY,dz=z-motion_.hostZ;
        if(dx*dx+dy*dy+dz*dz>128.*128.)return false;
        queued=loot_.append(epoch_,2,uint32_t(tier),x,y,z,accepted);
    }
    if(queued){char line[224]{};std::snprintf(line,sizeof(line),"Native empty container completed: epoch=%u event=%u subtype=%u chapter=%u tier=%u; original destruction retained.",accepted.epoch,accepted.eventId,unsigned(originalSubtype),unsigned(context.chapter),accepted.tier);combatLog(log_,line);}return queued;
}
bool GuestController::wantsCrateRewards()const{std::lock_guard lock(mutex_);return minecraftGameplayRoutes(guestMode_)&&requestedFreshLocked()&&game_;}
bool GuestController::requestedFreshLocked()const {uint64_t now=GetTickCount64();return guestBridgeMode(guestMode_)&&requestedActive_&&motionValid_&&motion_.epoch==epoch_&&now-lastActiveTime_<=750&&now-lastMotionTime_<=250;}
bool GuestController::activeLocked(const NativeSnapshot& s)const{return requestedFreshLocked()&&s.canReplace();}
bool GuestController::active()const{auto s=game_?game_->sample():NativeSnapshot{};std::lock_guard lock(mutex_);return activeLocked(s);}
bool GuestController::applyMotionLocked(const NativeSnapshot& s){
    // The verified window coroutine owns native movement, collision and its
    // landing until the real Minecraft client/server receipt is acknowledged.
    if(nativeTraversalHold_.load(std::memory_order_acquire))return false;
    if(!activeLocked(s))return false;
    Vec3 position{float(motion_.hostX*units_),float(motion_.hostY*units_),float(motion_.hostZ*units_)};
    if(!finite(position))return false;
    // Far teleports must be resynchronised by the host epoch/room handshake.
    Vec3 delta{position.x-s.position.x,position.y-s.position.y,position.z-s.position.z};if(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>units_*units_*1024)return false;
    float bodyYaw=motion_.yaw-(motion_.cameraMode==2?180.f:0.f);
    float yaw=-std::remainder(bodyYaw,360.f)*Radians;Vec3 speed{};
    bool ok=hookdetail::write(s.playerAddress+0x94,&position,12)&&hookdetail::write(s.playerAddress+0x110,&position,12)&&hookdetail::write(s.playerAddress+0xa4,&yaw,4)&&hookdetail::write(s.playerAddress+0x104,&speed,12);
    // Health stays positive natively; incoming attacks are emitted before subtraction.
    return ok;
}
bool GuestController::suppressPlayerMove()const{if(nativeTraversalHold_.load(std::memory_order_acquire))return false;OwnHookTimer cost(log_,OwnHook::Player);auto s=game_?game_->sample():NativeSnapshot{};cost.stateSampled();std::lock_guard lock(mutex_);return activeLocked(s)&&((motion_.flags&MotionDead)||motion_.health<=0);}
void GuestController::beforePlayerMove(void* self){
    OwnHookTimer cost(log_,OwnHook::Player);cost.stateSampled();
    auto s=game_?game_->sample():NativeSnapshot{};std::lock_guard lock(mutex_);if(reinterpret_cast<uintptr_t>(self)!=s.playerAddress||!applyMotionLocked(s))return;
    // The guest resolves terrain and placed blocks. Disable RE4 scene/object
    // collision only for this original move call, then restore the exact flags.
    uint16_t flags{};uintptr_t address=s.playerAddress+0x2ce;
    if(hookdetail::copy(address,&flags,2)){uint16_t off=flags&~uint16_t(0x300);if(hookdetail::write(address,&off,2)){playerCollisionOwned=true;playerCollisionFlags=flags;playerCollisionAddress=address;}}
}
void GuestController::afterPlayerMove(void* self){
    if(playerCollisionOwned){hookdetail::write(playerCollisionAddress,&playerCollisionFlags,2);playerCollisionOwned=false;}
    bool applied{};
    {OwnHookTimer cost(log_,OwnHook::Player);auto s=game_?game_->sample():NativeSnapshot{};cost.stateSampled();std::lock_guard lock(mutex_);if(reinterpret_cast<uintptr_t>(self)==s.playerAddress)applied=applyMotionLocked(s);}
    if(applied)originalMatrix(self,nullptr);
}
void GuestController::beforeModelMatrix(void* self){
    OwnHookTimer cost(log_,OwnHook::Matrix);
    std::lock_guard lock(mutex_);if(!requestedFreshLocked()||!game_)return;
    uintptr_t p=reinterpret_cast<uintptr_t>(self);
    // Most updated models are scenery/effects/equipment. Their matrices are
    // never owned here, so avoid validating every camera field for each model.
    if(p!=game_->currentPlayerAddress()&&(!minecraftGameplayRoutes(guestMode_)||blocks_.empty()||!member(enemyManager_,p)))return;
    auto s=game_->sample();cost.stateSampled();if(!activeLocked(s))return;
    if(p==s.playerAddress){applyMotionLocked(s);return;}
    if(!minecraftGameplayRoutes(guestMode_)||blocks_.empty()||!member(enemyManager_,p))return;EnemyData e{};if(!readEnemy(p,e))return;
    Vec3 from{(e.old.x+e.offset.x)/units_,(e.old.y+e.offset.y)/units_,(e.old.z+e.offset.z)/units_},to{(e.position.x+e.offset.x)/units_,(e.position.y+e.offset.y)/units_,(e.position.z+e.offset.z)/units_};
    if(std::abs(to.x-from.x)>16||std::abs(to.y-from.y)>16||std::abs(to.z-from.z)>16)return;
    Vec3 at=clipEnemyMotion(from,to,e.radius/units_,e.height/units_,blocks_);
    if(std::abs(at.x-to.x)+std::abs(at.y-to.y)+std::abs(at.z-to.z)<1e-5f)return;
    Vec3 result{at.x*units_-e.offset.x,at.y*units_-e.offset.y,at.z*units_-e.offset.z};hookdetail::write(p+0x94,&result,12);
}
bool GuestController::constrainEnemyAtari(void* self){
    OwnHookTimer cost(log_,OwnHook::EnemyCollision);bool changed{};uint32_t id{};Vec3 delta{};size_t count{};
    {
        std::lock_guard lock(mutex_);if(!minecraftGameplayRoutes(guestMode_)||!requestedFreshLocked()||!blockRegionValid_||!game_||!collisionTranslate_)return false;
        // Player/effect allocations fail manager membership or the native
        // enemy-kind check; no extra pPL slot read is needed for this path.
        uintptr_t p=reinterpret_cast<uintptr_t>(self);if(!member(enemyManager_,p))return false;
        EnemyData e{};if(!readEnemy(p,e)||nativeBreakableKind(e.kind)||!(e.atariFlags&0x100))return false;
        Vec3 from{(e.old.x+e.offset.x)/units_,(e.old.y+e.offset.y)/units_,(e.old.z+e.offset.z)/units_},to{(e.position.x+e.offset.x)/units_,(e.position.y+e.offset.y)/units_,(e.position.z+e.offset.z)/units_};
        if(std::abs(to.x-from.x)>16||std::abs(to.y-from.y)>16||std::abs(to.z-from.z)>16)return false;
        float radius=e.radius/units_,height=e.height/units_;
        // Immutable bounds are published with the packet. Actors outside this
        // tiny guest region need no full game/camera read or collider sweep.
        for(int a=0;a<3;++a){float lo=std::min(axis(from,a),axis(to,a))-(a==1?0:radius),hi=std::max(axis(from,a),axis(to,a))+(a==1?height:radius);if(hi<minimum(blockRegion_,a)||lo>maximum(blockRegion_,a))return false;}
        uint64_t now=GetTickCount64();if(!lastCollisionSample_||now-lastCollisionSample_>=8){collisionSnapshot_=game_->sample();lastCollisionSample_=now;cost.stateSampled();}
        if(!activeLocked(collisionSnapshot_))return false;
        Vec3 at=clipEnemyMotion(from,to,radius,height,blocks_);
        delta={(at.x-to.x)*units_,(at.y-to.y)*units_,(at.z-to.z)*units_};
        if(std::abs(delta.x)+std::abs(delta.y)+std::abs(delta.z)<.01f)return false;
        // This exact helper is used by native scene adjusters. It preserves
        // animated matrices and movement caches; no AI or animation is rerun.
        reinterpret_cast<CollisionTranslate>(collisionTranslate_)(self,&delta);changed=true;id=e.guid+1;count=blocks_.size();
    }
    // Only initial corrections are logged. No per-frame/collider diagnostics.
    thread_local unsigned logged{};if(changed&&logged++<8){char line[224]{};std::snprintf(line,sizeof(line),"Native enemy placed-block collision: target=%u count=%u correction=(%.3f,%.3f,%.3f) native units.",id,unsigned(count),double(delta.x),double(delta.y),double(delta.z));combatLog(log_,line);}return changed;
}
bool attachHostDamagePosition(HostDamage& damage,Vec3 source,float units,Vec3 feet){
    if(damage.kind!=1||(damage.flags&DamageFatal)||!finite(source)||!finite(feet)||!std::isfinite(units)||units<=0)return false;
    Vec3 p{source.x/units,source.y/units,source.z/units};
    if(!finite(p)||!bounded(p.x)||!bounded(p.y)||!bounded(p.z))return false;
    double dx=double(p.x)-feet.x,dy=double(p.y)-feet.y,dz=double(p.z)-feet.z;
    if(dx*dx+dy*dy+dz*dz>128.*128.)return false;
    damage.sourceX=p.x;damage.sourceY=p.y;damage.sourceZ=p.z;damage.flags|=DamageHasPosition;return true;
}
void GuestController::recordDamageLocked(float raw,uint32_t kind,uint32_t attacker,uint32_t flags,const Vec3* nativeSource){
    // BeamNG has no verified mapping from a native bullet/knife to physical
    // nodes. Keep the owned hidden native proxy alive, without inventing car HP.
    if(vehicleMode_||!std::isfinite(raw)||raw<=0||damage_.size()>=128)return;
    HostDamage d{};d.epoch=epoch_;d.eventId=++nextDamageId_;d.amount=std::min(raw/50.f,1024.f);d.kind=kind;d.attackerId=attacker;d.flags=flags;
    if(nativeSource)attachHostDamagePosition(d,*nativeSource,units_,{float(motion_.hostX),float(motion_.hostY),float(motion_.hostZ)});
    damage_.push_back(d);
    if(log_&&damageDiagnostics_.size()<128)damageDiagnostics_.push_back({d.epoch,d.eventId,kind,d.flags,raw,d.amount,motion_.health});
}
int GuestController::interceptLife(void* model,int damage,int randomRange,uint32_t flags){
    auto s=game_?game_->sample():NativeSnapshot{};
    if(reinterpret_cast<uintptr_t>(model)==s.playerAddress&&leonProtected(s))return s.health;
    bool playerRetained{},repeatRetained{};int retainedHp{},beforeHp{},rawNative=damage;
    DamageClaim observedClaim=DamageClaim::Unrelated;MeleeDiagnostic diagnostic{};
    {
        std::lock_guard lock(mutex_);
        const bool physicsActive=activeLocked(s);
        const bool heldProxy=vehicleVisualProxy_.protects(vehicleMode_,epoch_,s.canReplace(),GetTickCount64());
        if(physicsActive||heldProxy){
            uintptr_t address=reinterpret_cast<uintptr_t>(model);
            if(address==s.playerAddress){recordDamageLocked(float(std::max(damage,0)),0);playerRetained=true;retainedHp=s.health;}
            EnemyData enemy{};NativeDamageIdentity info{};
            // ObjItemBox's real consumer supplies fixed 1000/9999 damage for a
            // valid weapon hit. Scaling that into Minecraft creature health
            // prevents its native one-hit destruction/drop/save routine.
            if(physicsActive&&!playerRetained&&member(enemyManager_,address)&&readEnemy(address,enemy)&&!nativeBreakableKind(enemy.kind)&&damageIdentity(address,info)){
                int selectedDamage{};
                auto claim=damageLedger_.consume(epoch_,enemy.guid+1,address,info,GetTickCount64(),selectedDamage);
                if(claim==DamageClaim::AlreadyApplied){repeatRetained=true;retainedHp=enemy.hp;}
                if(claim==DamageClaim::Apply){
                    observedClaim=claim;beforeHp=enemy.hp;diagnostic={epoch_,0,enemy.guid+1,address,selectedDamage/50.f};
                    auto prior=std::find_if(meleeDiagnostics_.begin(),meleeDiagnostics_.end(),[&](const MeleeDiagnostic& d){return d.epoch==epoch_&&d.id==enemy.guid+1&&d.model==address;});if(prior!=meleeDiagnostics_.end())diagnostic=*prior;
                    // A native zero-damage body part remains immune. For an
                    // actual damaging reaction the Minecraft hit is applied once.
                    if(damage>0){damage=selectedDamage;randomRange=0;}
                }
            }
        }
    }
    if(playerRetained||repeatRetained)return retainedHp;
    int result=originalLife(model,damage,randomRange,flags);
    if(observedClaim==DamageClaim::Apply){
        int16_t afterHp=static_cast<int16_t>(result);hookdetail::copy(reinterpret_cast<uintptr_t>(model)+0x324,&afterHp,2);
        char line[320]{};std::snprintf(line,sizeof(line),"Native melee ledger consumed once: epoch=%u event=%u target=%u rawMC=%.3f originalNative=%d appliedNative=%d HP=%d->%d result=%s.",diagnostic.epoch,diagnostic.event,diagnostic.id,double(diagnostic.rawMinecraft),rawNative,damage,beforeHp,int(afterHp),rawNative>0?"native-reaction":"zero-damage-body-part-immune");combatLog(log_,line);
    }
    return result;
}
bool GuestController::interceptEnemyAttack(void*,void* info,void* from,void* to){
    auto s=game_?game_->sample():NativeSnapshot{};
    if(leonProtected(s))return true; // verified EmAtkSetDamagePL/direct-zero caller: player only
    std::lock_guard lock(mutex_);
    if(!activeLocked(s)&&!vehicleVisualProxy_.protects(vehicleMode_,epoch_,s.canReplace(),GetTickCount64()))return false;
    uint16_t amount{};uint8_t attributes{};auto address=reinterpret_cast<uintptr_t>(info);
    if(!hookdetail::copy(address+8,&amount,2)||!hookdetail::copy(address+0xa,&attributes,1))return false;
    Vec3 a{},b{};bool sourceValid=hookdetail::copy(reinterpret_cast<uintptr_t>(from),&a,12)&&finite(a);
    if(minecraftGameplayRoutes(guestMode_)&&sourceValid&&hookdetail::copy(reinterpret_cast<uintptr_t>(to),&b,12)&&finite(b)&&blockedByGuestBlocks({a.x/units_,a.y/units_,a.z/units_},{b.x/units_,b.y/units_,b.z/units_},blocks_))return true;
    recordDamageLocked((attributes&8)?motion_.maxHealth*50.f:float(amount),1,0,(attributes&8)?DamageFatal:0U,sourceValid?&a:nullptr);return true;
}
void GuestController::refreshEnemiesLocked(const NativeSnapshot& s){
    uint64_t now=GetTickCount64();if(!nativeEnemyRefreshDue(lastEnemyTime_,now))return;lastEnemyTime_=now;
    enemies_.clear();enemyAddresses_.clear();Manager m{};if(!activeLocked(s)||!readManager(enemyManager_,m))return;
    for(uint32_t i=0;i<m.count&&enemies_.size()<256;++i){EnemyData e{};if(!readEnemy(m.array+i*m.stride,e))continue;float x=(e.position.x-s.position.x)/units_,y=(e.position.y-s.position.y)/units_,z=(e.position.z-s.position.z)/units_;if(x*x+y*y+z*z>128*128)continue;readHitBounds(e);if(minecraftGameplayRoutes(guestMode_))readCrateBodyBounds(e);if(appendUniqueEnemy(enemies_,packet(e,units_,minecraftGameplayRoutes(guestMode_))))enemyAddresses_.push_back({e.guid+1,e.address});}
}
bool GuestController::shoveVehicleContact(const GuestMelee& attack,const GuestMotion& impactMotion,uintptr_t address,const NativeSnapshot& s){
    float units{};GuestMotion motion{};
    {std::lock_guard lock(mutex_);if(!vehicleMode_||attack.epoch!=epoch_||!activeLocked(s)||!vehicleSatManager_||!collisionTranslate_||!originalAtari)return false;units=units_;motion=motion_;}
    EnemyData enemy{};
    if(!member(enemyManager_,address)||!readEnemy(address,enemy)||enemy.guid+1!=attack.targetId||
       nativeBreakableKind(enemy.kind)||!nativeGanadoKind(enemy.kind)||!(enemy.atariFlags&0x100)||
       !vehicleImpactGeometryInReach(attack,packet(enemy,units),motion)||!vehicleImpactInReach(attack,packet(enemy,units),impactMotion))return false;
    auto shove=vehicleShoveMetres(attack);float length=std::hypot(shove.x,shove.z);
    if(length<=0)return false;
    const unsigned steps=static_cast<unsigned>(std::clamp(std::ceil(length/.2f),1.f,10.f));
    Vec3 step{shove.x*units/steps,0.f,shove.z*units/steps};
    for(unsigned i=0;i<steps;++i){
        {std::lock_guard lock(mutex_);if(!vehicleMode_||attack.epoch!=epoch_||!activeLocked(s))return i!=0;}
        EnemyData fresh{};if(!member(enemyManager_,address)||!readEnemy(address,fresh)||fresh.guid!=enemy.guid||!(fresh.atariFlags&0x100))return i!=0;
        //592E40 translates the position, existing animated part matrices and
        // movement cache together. The native594FC0 call then clips against
        // actual room SAT in the same flags0 mode used by cEm10's own movement.
        reinterpret_cast<CollisionTranslate>(collisionTranslate_)(reinterpret_cast<void*>(address),&step);
        originalAtari(reinterpret_cast<void*>(vehicleSatManager_),nullptr,reinterpret_cast<void*>(address),0);
    }
    return true;
}
void GuestController::beforeSchedule(){
    // The catch decision runs inside the original scheduler, before ordinary
    // damage interception. Protect the hidden vehicle proxy before that call,
    // then restore its exact timer in afterSchedule, even after a transition.
    NativeSnapshot grabSnapshot{};bool grabCandidate=false;
    {std::lock_guard lock(mutex_);grabCandidate=game_&&installed_&&
        ((leonProtectionFlags_.load(std::memory_order_acquire)&1)!=0||(vehicleMode_&&
        (requestedFreshLocked()||vehicleVisualProxy_.protects(vehicleMode_,epoch_,true,GetTickCount64()))));}
    if(grabCandidate){OwnHookTimer cost(log_,OwnHook::ScheduleCapture);grabSnapshot=game_->sample();cost.stateSampled();}
    const bool protectLeon=leonProtected(grabSnapshot);
    leonGrabScope.begin(protectLeon,grabSnapshot.playerAddress,hookdetail::copy,hookdetail::write);
    // Keep the actual live Leon full; fatal attacks are intercepted before
    // subtraction as well. Never revive a corpse or write a guest's HP.
    if(protectLeon&&grabSnapshot.maxHealth>0&&grabSnapshot.health<=grabSnapshot.maxHealth){
        hookdetail::write(grabSnapshot.globalAddress+0x4fb4,&grabSnapshot.maxHealth,sizeof(grabSnapshot.maxHealth));
    }
    {
        std::lock_guard lock(mutex_);
        const bool protects=installed_&&vehicleMode_&&
            (activeLocked(grabSnapshot)||vehicleVisualProxy_.protects(vehicleMode_,epoch_,grabSnapshot.canReplace(),GetTickCount64()));
        vehicleGrabScope.begin(protects,grabSnapshot.playerAddress,hookdetail::copy,hookdetail::write);
    }
    NativeSnapshot s{};std::vector<QueuedAttack> attacks;std::vector<BlockBox> boxes;std::vector<std::pair<uint32_t,uintptr_t>> addresses;float units{};Vec3 feet{};bool vehicle{};GuestMotion vehicleMotion{};BridgeMode scheduledMode{BridgeMode::Native};
    {
        // Measure only our capture/validation, never the native dispatcher or
        // native health function called below. Snapshot refresh is at most20Hz.
        OwnHookTimer cost(log_,OwnHook::ScheduleCapture);
        {std::lock_guard lock(mutex_);if(!requestedFreshLocked()||!game_)return;}
        if(grabCandidate)s=grabSnapshot;else{s=game_->sample();cost.stateSampled();}
        std::lock_guard lock(mutex_);if(!activeLocked(s))return;applyMotionLocked(s);refreshEnemiesLocked(s);if((motion_.flags&MotionDead)||motion_.health<=0)pendingMelee_.clear();else attacks.swap(pendingMelee_);units=units_;feet={float(motion_.hostX),float(motion_.hostY),float(motion_.hostZ)};vehicle=vehicleMode_;scheduledMode=guestMode_;vehicleMotion=motion_;if(!attacks.empty()){if(minecraftGameplayRoutes(scheduledMode))boxes=blocks_;addresses=enemyAddresses_;}
    }
    if(attacks.empty())return;
    Manager m{};if(!readManager(enemyManager_,m))return;
    for(const auto& queued:attacks){const auto& attack=queued.attack;if(GetTickCount64()>queued.expires){char line[192]{};std::snprintf(line,sizeof(line),"Guest combat route: epoch=%u event=%u target=%u rawMC=%.3f result=native-reaction-busy-expired.",attack.epoch,attack.eventId,attack.targetId,double(attack.damage));combatLog(log_,line);continue;}
        {std::lock_guard lock(mutex_);if(attack.epoch!=epoch_||scheduledMode!=guestMode_||!activeLocked(s))continue;}
        auto found=std::find_if(addresses.begin(),addresses.end(),[&](const auto& entry){return entry.first==attack.targetId;});if(found==addresses.end())continue;
        uintptr_t address=found->second;
        // Cached packets find the allocation only. Membership, exact GUID,
        // live flags, HP and animated shape are freshly read before each call.
        if(address<m.array||(address-m.array)%m.stride||(address-m.array)/m.stride>=m.count)continue;
        EnemyData e{};if(!readEnemy(address,e,true)||e.guid+1!=attack.targetId)continue;if(minecraftGameplayRoutes(scheduledMode))readCrateBodyBounds(e);Enemy current=packet(e,units,minecraftGameplayRoutes(scheduledMode));if((vehicle?(!vehicleImpactGeometryInReach(attack,current,vehicleMotion)||!vehicleImpactInReach(attack,current,queued.impactMotion)):!meleeInReach(attack,current,feet))||(nativeBreakableKind(e.kind)&&!nativeBreakableAttack(attack.flags)))continue;
        if(minecraftGameplayRoutes(scheduledMode)&&guestAttackUsesBlockRay(attack.flags)&&blockedByGuestBlocks({float(attack.attackX),float(attack.attackY),float(attack.attackZ)},{float(current.x+(current.minX+current.maxX)*.5),float(current.y+(current.minY+current.maxY)*.5),float(current.z+(current.minZ+current.maxZ)*.5)},boxes))continue;
        int amount=std::clamp(int(std::lround(attack.damage*50.f)),1,32767);
        bool nativeReaction=false,reactionArmed=false;
        if((nativeGanadoKind(e.kind)||nativeBreakableKind(e.kind))&&originalDamageSet){
            bool pending{};{std::lock_guard lock(mutex_);pending=damageLedger_.pending(epoch_,attack.targetId,GetTickCount64());}
            if(pending||(e.damageFlags&1)||e.damageTimer){
                // A native reaction already owns this damage-info record. Keep
                // our accepted event bounded instead of replacing native state,
                // losing the hit, or leaving HP0 with no death consumer.
                std::lock_guard lock(mutex_);if(attack.epoch==epoch_&&scheduledMode==guestMode_&&activeLocked(s)&&!(motion_.flags&MotionDead)&&motion_.health>0&&pendingMelee_.size()<64)pendingMelee_.push_back(queued);continue;
            }
            float length=std::sqrt(attack.directionX*attack.directionX+attack.directionY*attack.directionY+attack.directionZ*attack.directionZ);
            Vec3 from{float(attack.attackX*units),float(attack.attackY*units),float(attack.attackZ*units)};
            // The original explosion reaction supplies a single actor's
            // knockdown/fall consumer at ten mph and above. Damage still
            // comes from the actual impact and the one-time native ledger;
            // no radial explosion dispatcher or extra area hit is called.
            uint32_t weapon=vehicle&&vehicleKnockdownImpact(attack)?0x12U:guestNativeDamageWeaponForFlags(attack.flags);
            if(!nativeBreakableKind(e.kind)&&(attack.flags&~(HitCritical|HitAlly))==0&&meleeDispatcher_&&length>.01f&&length<1.8f){
                float distance=attack.maxReach*units/length;
                Vec3 to{from.x+attack.directionX*distance,from.y+attack.directionY*distance,from.z+attack.directionZ*distance};
                auto previous=reactionScope;reactionScope={e.address,false};
                // Weapon16 and six stack arguments are traced from the original
                // knife animation. Flags1/2 suppress native impact/miss effects.
                reinterpret_cast<MeleeDispatch>(meleeDispatcher_)(reinterpret_cast<void*>(s.playerAddress),&from,&to,16,3,attack.maxReach*units);
                nativeReaction=reactionScope.selected;reactionScope=previous;
            }
            if(!nativeReaction){
                bool protectedWeaponRejected=partWeaponFilter_&&reinterpret_cast<int(__cdecl*)(uint32_t)>(partWeaponFilter_)(weapon)!=0;
                uintptr_t shape=nearestHitShape(e,from,protectedWeaponRejected);NativeHitShape fresh{};
                if(!shape&&e.shapeCount){char line[192]{};std::snprintf(line,sizeof(line),"Guest combat route: epoch=%u event=%u target=%u rawMC=%.3f result=native-protected-body-part-immune.",attack.epoch,attack.eventId,attack.targetId,double(attack.damage));combatLog(log_,line);continue;}
                // The guest has already performed its actual hit/exposure test.
                // Install a native pending hit on a real active native part;
                // original enemy code owns stagger, combustion and death.
                // Calling the radial dispatcher would produce extra area hits.
                if(shape&&hookdetail::copy(shape,&fresh,sizeof(fresh))&&nativeHitPartAcceptsWeapon(fresh.flags,protectedWeaponRejected)){
                    Vec3 delta{from.x-e.position.x,from.y-e.position.y,from.z-e.position.z};float squared=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
                    std::lock_guard lock(mutex_);if(attack.epoch==epoch_&&scheduledMode==guestMode_&&activeLocked(s)&&!(motion_.flags&MotionDead)&&motion_.health>0){originalDamageSet(reinterpret_cast<void*>(e.address+0x328),nullptr,fresh.flags&0x20U,10,weapon,&from,squared,reinterpret_cast<void*>(shape));nativeReaction=true;}
                }
            }
            NativeDamageIdentity info{};
            // A crate receives the verified pending shape hit, then its own
            // move consumer owns original damage, break effects and scripts.
            // It must not acquire the creature-damage override ledger.
            if(nativeReaction&&!nativeBreakableKind(e.kind)){
                bool identified=damageIdentity(e.address,info);
                    bool armed{};{std::lock_guard lock(mutex_);armed=identified&&attack.epoch==epoch_&&scheduledMode==guestMode_&&activeLocked(s)&&damageLedger_.arm(epoch_,e.guid+1,attack.eventId,e.address,info,amount,GetTickCount64());
                        if(armed&&log_){meleeDiagnostics_.erase(std::remove_if(meleeDiagnostics_.begin(),meleeDiagnostics_.end(),[&](const MeleeDiagnostic& d){return d.id==e.guid+1;}),meleeDiagnostics_.end());if(meleeDiagnostics_.size()>=64)meleeDiagnostics_.erase(meleeDiagnostics_.begin());meleeDiagnostics_.push_back({epoch_,attack.eventId,e.guid+1,e.address,attack.damage});}}
                    reactionArmed=armed;
                    if(!armed&&meleeCancel_){
                        // Exact native cDmgInfo::clear, verified at setter+0xa0,
                        // cancels our just-created reaction before HP fallback.
                        reinterpret_cast<ModelFn>(meleeCancel_)(reinterpret_cast<void*>(e.address+0x328),nullptr);nativeReaction=false;
                    }
            }
        }
        // Actors with no verified common Ganado consumer or no readable active
        // native body part keep the bounded generic health route.
        // A box is never reduced through the generic HP route: its original
        // consumer owns destruction, item drops and room script bookkeeping.
        if(!nativeReaction&&!nativeBreakableKind(e.kind)){bool wanted{};{std::lock_guard lock(mutex_);wanted=attack.epoch==epoch_&&scheduledMode==guestMode_&&activeLocked(s)&&!(motion_.flags&MotionDead)&&motion_.health>0;}if(wanted)originalLife(reinterpret_cast<void*>(e.address),amount,0,0);}
        bool shoved=vehicle&&shoveVehicleContact(attack,queued.impactMotion,e.address,s);
        int16_t afterHp=e.hp;hookdetail::copy(e.address+0x324,&afterHp,2);
        char line[352]{};std::snprintf(line,sizeof(line),"Guest combat route: epoch=%u event=%u target=%u kind=%u rawMC=%.3f mappedNative=%d flags=%u HP=%d->%d vehicleShove=%u vehicleKnockdown=%u result=%s.",attack.epoch,attack.eventId,attack.targetId,unsigned(e.kind),double(attack.damage),amount,attack.flags,int(e.hp),int(afterHp),unsigned(shoved),unsigned(vehicle&&vehicleKnockdownImpact(attack)&&nativeReaction),nativeReaction?(nativeBreakableKind(e.kind)?"native-crate-original-damage-queued":reactionArmed?"native-reaction-armed":"native-reaction-selected-no-ledger"):nativeBreakableKind(e.kind)?"crate-no-native-part-skipped":"HP-fallback");combatLog(log_,line);
    }
}
void GuestController::afterSchedule(){
    const auto leonPlayer=leonGrabScope.ownsProtection()&&game_&&game_->currentPlayerCharacter()==0
        ?game_->currentPlayerAddress():0;
    leonGrabScope.end(leonPlayer,hookdetail::copy,hookdetail::write);
    vehicleGrabScope.end(game_?game_->currentPlayerAddress():0,hookdetail::copy,hookdetail::write);
    OwnHookTimer cost(log_,OwnHook::Enemies);{std::lock_guard lock(mutex_);if(!requestedFreshLocked()||!game_||!nativeEnemyRefreshDue(lastEnemyTime_,GetTickCount64()))return;}auto s=game_->sample();cost.stateSampled();std::lock_guard lock(mutex_);refreshEnemiesLocked(s);
}
void GuestController::setLeonProtection(bool requested,BridgeMode selectedMode){
    leonProtectionFlags_.store(uint8_t((requested?1:0)|(uint8_t(selectedMode)<<1)),std::memory_order_release);
}
bool GuestController::leonProtected(const NativeSnapshot& s)const{
    const auto flags=leonProtectionFlags_.load(std::memory_order_acquire);
    const LeonProtectionContext context{static_cast<BridgeMode>(flags>>1),
        s.valid&&s.playing,s.alive,s.paused,s.cutscene,s.valid&&s.character<=6,s.character==0};
    return installed_&&leonProtectionActive((flags&1)!=0,context);
}
bool GuestController::preventNativeGrab()const{
    if(!installed_||!game_)return false;
    // Catch routing needs current actor/game state, independent of a camera
    // swap, guest mesh, source pause or native world-render bootstrap.
    const auto s=game_->protectionState();
    const auto flags=leonProtectionFlags_.load(std::memory_order_acquire);
    return guestGrabProtectionActive(static_cast<BridgeMode>(flags>>1),s.valid,s.alive,s.playing,s.cutscene)
        ||leonProtected(s);
}
}
