#pragma once
#include "game_access.hpp"
#include <array>

namespace re4craft {
// Exact ordinary em10 hold return addresses, never ranges of arbitrary QTEs.
struct NativeGrabCallSites {
    std::array<uintptr_t,6> returns{};
    bool valid()const;
    bool contains(uintptr_t caller)const;
};

class NativeGrabHooks {
public:
    using Prevent=bool (*)(void*);
    // PlGachaGet is verified int __cdecl(), no parameters, with a plain RET.
    // Eligibility belongs to GuestController and is checked only at a proven
    // hold caller. Other callers still obtain the original game's counter.
    bool install(Prevent prevent,void* context,LogFn log=nullptr);
    bool installed()const{return target_!=0;}
    int counter(uintptr_t caller)const;
#ifdef RE4CRAFT_GRAB_FIXTURE
    bool installFixture(uintptr_t target,const NativeGrabCallSites& sites,Prevent prevent,void* context);
    void removeFixture();
#endif
private:
    using Get=int (__cdecl*)();
    bool installVerified(uintptr_t target,const NativeGrabCallSites&,Prevent,void*,LogFn);
    uintptr_t target_{};
    NativeGrabCallSites sites_{};
    Get original_{};
    Prevent prevent_{};
    void* context_{};
};
}
