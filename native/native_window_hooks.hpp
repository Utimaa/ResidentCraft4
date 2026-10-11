#pragma once
#include "game_access.hpp"
#include <atomic>

namespace re4craft {
namespace windowdetail {
// Unique retail1.1.0 JumpOut/JumpIn prompt callback setter and its actual
// cdecl bool(cEmWindow*) coroutine entry. Relocated globals/calls are masked.
inline constexpr const char* Setter=
    "55 8B EC 8B 45 08 6A 00 6A 02 6A 02 50 68 ? ? ? ? 6A 14 E8 ? ? ? ? 83 C4 18 C7 05 ? ? ? ? 01 00 00 00 5D C3";
inline constexpr const char* Callback=
    "55 8B EC 83 EC 20 A1 ? ? ? ? 33 C5 89 45 FC 57 8B 7D 08 85 FF 75 20 68 ? ? ? ? 57 57 E8 ? ? ? ? 83 C4 0C 32 C0 5F 8B 4D FC 33 CD E8 ? ? ? ? 8B E5 5D C3 8B 87 68 06 00 00";
}
class NativeWindowHooks {
public:
    using Begin=uint64_t (*)(void*);
    using Complete=void (*)(void*,uint64_t,bool);
    bool install(Begin begin,Complete complete,void* context,LogFn log=nullptr);
    bool installed()const{return target_!=0;}
    bool invoke(void* window);
    void observeOriginalSuccess(void* window);
#ifdef RE4CRAFT_WINDOW_FIXTURE
    bool installFixture(uintptr_t target,Begin begin,Complete complete,void* context,uintptr_t successTarget=0);
    void removeFixture();
#endif
private:
    using Original=bool (__cdecl*)(void*);
    bool installVerified(uintptr_t,uintptr_t,Begin,Complete,void*,LogFn);
    Original original_{};Begin begin_{};Complete complete_{};void* context_{};
    uintptr_t target_{},successTarget_{};
    std::atomic<uint64_t> pendingToken_{};
    std::atomic<uintptr_t> pendingWindow_{};
};
}
