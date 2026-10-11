#pragma once
#include "game_access.hpp"
#include "../protocol/re4craft_protocol.h"
#include "combat_ledger.hpp"
#include "loot_policy.hpp"
#include "vehicle_interaction.hpp"
#include "beamng_mode.hpp"
#include <atomic>
#include <mutex>
#include <vector>

namespace re4craft {
// Pure, bounded validation/collision helpers shared with executable fixtures.
bool validGuestMotion(const GuestMotion&,uint32_t epoch,bool vehicleMode=false);
bool validGuestMotion(const GuestMotion&,uint32_t epoch,BridgeMode mode);
bool validBlockBox(const BlockBox&);
bool nativeEnemyKind(uint32_t kind);
constexpr bool nativeBreakableKind(uint32_t kind){return kind==0x53;}
constexpr bool nativeBreakableAttack(uint32_t flags){return (flags&(HitFire|HitHazard|HitAlly))==0;}
bool attachHostDamagePosition(HostDamage&,Vec3 nativeSource,float unitsPerBlock,Vec3 guestFeet);
Vec3 clipEnemyMotion(Vec3 from,Vec3 to,float radius,float height,const std::vector<BlockBox>&);
bool meleeInReach(const GuestMelee&,const Enemy&,Vec3 guestFeet);
bool blockedByGuestBlocks(Vec3 from,Vec3 to,const std::vector<BlockBox>&);
bool validEnemyRecord(const Enemy&);
bool appendUniqueEnemy(std::vector<Enemy>&,const Enemy&);
// SDK YARARE_INFO geometry copied read-only from the native actor. The hit
// tester transforms these shapes by the actor/animated part's mat_C.
struct NativeHitShape {Vec3 offset{},cross{};float radius{},height{},extent{};uint16_t flags{};int16_t part{};float length{},distance{};uint32_t next{};};
static_assert(sizeof(NativeHitShape)==0x34);
struct NativeActorBounds {Vec3 minimum{},maximum{};bool valid{};};
bool appendNativeHitShapeBounds(NativeActorBounds&,const NativeHitShape&,const float matrix[3][4],Vec3 actorPosition);
bool guestAttackUsesBlockRay(uint32_t flags);
constexpr bool nativeEnemyRefreshDue(uint64_t last,uint64_t now){return !last||(now>=last&&now-last>=50);}
constexpr bool nativeHitPartAcceptsWeapon(uint16_t shapeFlags,bool protectedWeaponRejected){return (shapeFlags&1)&&!((shapeFlags&0x10)&&protectedWeaponRejected);}
#ifdef RE4CRAFT_ACCESS_FIXTURE
// Owned-memory x86 fixture only; absent from production builds.
class GuestController;
void setGuestDamageSetterFixture(uintptr_t address);
void setGuestCrateLootFixture(uintptr_t move,uintptr_t allocator,uintptr_t returnAddress,GuestController* owner);
void runGuestCrateMoveFixture(void* actor);
uintptr_t guestItemSetFixture();
#endif

// All writes and calls execute on RE4's verified scheduler/player update thread.
// Network/render threads only publish bounded data and consume snapshots.
class GuestController {
public:
    bool installHooks(GameAccess*,LogFn=nullptr);
    void setActive(bool active,uint32_t epoch,float unitsPerBlock=1000.f,bool vehicleMode=false);
    void setActive(bool active,uint32_t epoch,float unitsPerBlock,BridgeMode mode);
    void setVehicleVisualHold(bool held,uint32_t epoch);
    void setNativeTraversalHold(bool held){nativeTraversalHold_.store(held,std::memory_order_release);}
    void setLeonProtection(bool requested,BridgeMode selectedMode);
    bool preventNativeGrab() const;
    bool updateMotion(const GuestMotion&);
    bool setBlocks(uint32_t epoch,const std::vector<BlockBox>&);
    bool applyMelee(const GuestMelee&); // queues, never calls game from caller
    std::vector<HostDamage> takeDamage();
    std::vector<HostLoot> takeLoot();
    bool retryLoot(const HostLoot&);
    bool interceptLoot(uintptr_t actor,uint32_t source,const Vec3* position,uint32_t id,uint32_t count);
    bool interceptCrateBreak(uintptr_t actor,uint32_t originalGuid,uint8_t originalSubtype);
    bool wantsCrateRewards() const;
    std::vector<Enemy> takeEnemies() const;
    bool active() const;
    bool installed() const { return installed_; }
    void restore() { setActive(false,0,1000.f,BridgeMode::Native); }
    void beforeSchedule();
    void afterSchedule();
    void beforePlayerMove(void*);
    bool suppressPlayerMove() const;
    void afterPlayerMove(void*);
    void beforeModelMatrix(void*);
    bool constrainEnemyAtari(void*);
    int interceptLife(void* model,int damage,int randomRange,uint32_t flags);
    bool interceptEnemyAttack(void* yarare,void* attackInfo,void* from,void* to);
private:
    mutable std::mutex mutex_;
    GameAccess* game_{};
    LogFn log_{};
    uintptr_t enemyManager_{},matrixFunction_{};
    uintptr_t collisionTranslate_{},vehicleSatManager_{};
    NativeSnapshot collisionSnapshot_{};
    uint64_t lastCollisionSample_{};
    uintptr_t meleeDispatcher_{},meleeCancel_{},partWeaponFilter_{};
    GuestMotion motion_{};
    uint32_t epoch_{},nextDamageId_{},lastMeleeId_{};
    float units_{1000.f};
    uint64_t lastActiveTime_{},lastMotionTime_{},lastEnemyTime_{};
    bool requestedActive_{},motionValid_{},installed_{},vehicleMode_{};
    BridgeMode guestMode_{BridgeMode::Native};
    std::atomic<uint8_t> leonProtectionFlags_{};
    std::atomic<bool> nativeTraversalHold_{};
    std::vector<BlockBox> blocks_;
    BlockBox blockRegion_{};
    bool blockRegionValid_{};
    struct QueuedAttack {GuestMelee attack;uint64_t expires;GuestMotion impactMotion{};};
    std::vector<QueuedAttack> pendingMelee_;
    std::vector<std::pair<uint32_t,uintptr_t>> enemyAddresses_;
    std::vector<HostDamage> damage_;
    std::vector<Enemy> enemies_;
    CombatDamageLedger damageLedger_;
    VehicleContactLedger vehicleContacts_;
    VehicleMotionHistory vehicleMotionHistory_;
    BeamngVisualProxyRetention vehicleVisualProxy_;
    NativeLootQueue loot_;
    struct DamageDiagnostic {uint32_t epoch,event,kind,flags;float rawNative,rawMinecraft,guestHealth;};
    struct MeleeDiagnostic {uint32_t epoch,event,id;uintptr_t model;float rawMinecraft;};
    std::vector<DamageDiagnostic> damageDiagnostics_;
    std::vector<MeleeDiagnostic> meleeDiagnostics_;
    bool requestedFreshLocked() const;
    bool activeLocked(const NativeSnapshot&) const;
    bool leonProtected(const NativeSnapshot&) const;
    bool applyMotionLocked(const NativeSnapshot&);
    void recordDamageLocked(float raw,uint32_t kind,uint32_t attacker=0,uint32_t flags=0,const Vec3* nativeSource=nullptr);
    void refreshEnemiesLocked(const NativeSnapshot&);
    bool shoveVehicleContact(const GuestMelee&,const GuestMotion& impactMotion,uintptr_t address,const NativeSnapshot&);
};
}
