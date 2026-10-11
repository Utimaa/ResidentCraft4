#pragma once
#include <cstdint>
#include <cstddef>
#include <mutex>

namespace re4craft {
struct Vec3 { float x{}, y{}, z{}; };
struct GameLootContext {
    bool valid{};
    uint8_t chapter{0xff},character{0xff};
    uint32_t systemFlags{};
};
// A read-only identity/position witness for the exact native window callback.
// It intentionally has no camera or general guest-physics eligibility.
struct NativeTraversalState {
    bool valid{},alive{},playing{};
    uintptr_t globalAddress{},playerAddress{};
    uint32_t playerGuid{};
    uint16_t room{};
    Vec3 position{},angles{};
};
struct NativeSnapshot {
    bool valid{}, alive{}, playing{}, paused{}, cutscene{};
    Vec3 position{}, angles{}, cameraPosition{}, cameraTarget{};
    float view[3][4]{};       // RE4 column-vector affine matrix
    float projection[4][4]{}; // RE4/GameCube column-vector matrix
    float fovDegrees{}, deltaTime{};
    int16_t health{}, maxHealth{};
    uint16_t room{};
    uint8_t character{0xff}; // verified GLOBAL_WK::pl_type_4FC8; Leon=0
    // Raw SDK state retained for bounded transition diagnostics. SPF_CTRL is
    // a native processing-stop flag, not a keyboard Control-key indicator.
    uint32_t stopFlags{}, statusFlags{}, playerBeFlags{};
    uint8_t routine{};
    uint64_t keyOn{}, keyTrigger{};
    uintptr_t globalAddress{}, playerAddress{};
    // Captured only at the verified executed GX world boundary. Includes
    // native GX depth conversion and its actual post-projection raster matrix.
    bool rasterProjectionValid{};
    float rasterProjection[4][4]{}; // Direct3D column-vector clip matrix
    bool canReplace() const { return valid && alive && playing && !paused && !cutscene; }
};
using LogFn = void (*)(const char*);
constexpr uint32_t playerDisplayFlags(uint32_t flags,bool hidden){return hidden?flags|0x40000000U:flags&~0x40000000U;}
constexpr uint32_t refreshOwnedPlayerDisplayFlags(uint32_t flags,bool owned){return owned?playerDisplayFlags(flags,true):flags;}
constexpr bool canRefreshPlayerHide(bool owned,uint64_t heartbeat,uint64_t now,bool nativeCanReplace,bool sameGlobal){return owned&&heartbeat&&now>=heartbeat&&now-heartbeat<=250&&nativeCanReplace&&sameGlobal;}

// This adapter owns only the DPF_PL display bit. Verified camera, control and
// guest motion/combat writes are isolated in their respective controllers.
class GameAccess {
public:
    bool initialize(LogFn log = nullptr);
    bool initialized() const { return initialized_; }
    NativeSnapshot sample() const;
    NativeSnapshot protectionState() const; // actor/game eligibility only; never camera/physics authority
    GameLootContext lootContext() const;
    NativeTraversalState traversalState() const;
    // Refresh exact camera bytes while retaining the caller's eligibility/
    // player metadata. A changed globalAddress requires a new full sample.
    bool readCamera(NativeSnapshot& inout) const;
    uintptr_t currentPlayerAddress() const; // cheap scanned-slot read for actor membership gates
    uint8_t currentPlayerCharacter() const; // restore guards also work during camera transitions
    bool setReplacementVisible(bool replacementVisible);
    void restore();
    static constexpr uint32_t HidePlayerMask = 0x40000000;
#ifdef RE4CRAFT_ACCESS_FIXTURE
    void initializeReadFixture(uintptr_t globalSlot,uintptr_t playerSlot){
        globalSlot_=globalSlot;playerSlot_=playerSlot;initialized_=true;
    }
#endif
private:
    static void __cdecl nativeDrawHook(void* model);
    void refreshReplacementBeforeDraw(void* model);
    void restoreDisplayLocked();
    uintptr_t globalSlot_{}, playerSlot_{}, keyOn_{}, keyTrigger_{};
    uintptr_t hiddenGlobal_{};
    mutable std::mutex displayMutex_;
    LogFn log_{};
    uint64_t hideHeartbeat_{},lastHideResetLog_{};
    uint32_t hideResetCount_{},hideResetLogs_{};
    bool originalHidden_{}, ownsDisplayBit_{}, initialized_{};
};
}
