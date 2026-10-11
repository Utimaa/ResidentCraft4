#pragma once
#include "game_access.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>

namespace re4craft {
class CameraControlBridge;
// RE4 SDK KEY_BTN: aim, relock, fire, knife, manual briefcase/map input.
constexpr uint64_t HostBlockedButtons = 0x10ULL|0x20ULL|0x80ULL|0x800ULL|0x100000ULL|0x200000ULL|0x8000000000ULL|0x1000000000000ULL|0x2000000000000ULL;
constexpr uint64_t filterHostButtons(uint64_t buttons, bool active) { return active ? buttons & ~HostBlockedButtons : buttons; }
constexpr uint64_t HostMovementButtons=0xfULL|0x40ULL|0x100ULL;
constexpr uint64_t filterGuestMovement(uint64_t buttons,bool guest) { return guest?buttons&~HostMovementButtons:buttons; }
constexpr bool blockNativeSubscreen(uint32_t openFlags,bool active) {
    // Manual briefcase/map/puzzle only. Shops, saves, scripted item screens stay native.
    return active && (openFlags&(0x1U|0x2U|0x4U)) && !(openFlags&(0x10U|0x20U|0x80U|0x100U));
}
class HostControls {
public:
    bool installHooks(GameAccess* game, LogFn log=nullptr);
    void setActive(bool active);
    void setCamera(CameraControlBridge* camera) { camera_=camera; }
    void setScreenOpen(bool open) { screenOpen_.store(open,std::memory_order_release); }
    void setGuestAuthority(bool guest) { guestAuthority_.store(guest,std::memory_order_release); }
    void restore() { setActive(false); setHudHidden(false); }
    bool setHudHidden(bool hidden);
    bool installed() const { return installed_; }
    bool active() const;
    void afterPadRead();
private:
    GameAccess* game_{};
    CameraControlBridge* camera_{};
    LogFn log_{};
    std::atomic<bool> active_{};
    std::atomic<bool> screenOpen_{};
    std::atomic<bool> guestAuthority_{};
    std::atomic<uint64_t> lastActiveTime_{};
    uintptr_t keyOn_{},keyTrigger_{};
    uintptr_t globalSlot_{},hiddenHudGlobal_{};
    std::mutex hudMutex_;
    bool ownsHudBit_{},originalHudHidden_{};
    bool installed_{};
};
}
