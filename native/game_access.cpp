#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "game_access.hpp"
#include "vendor/minhook/include/MinHook.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// Signature bytes and SDK offsets are based on nipkownix/re4_tweaks.
// This reduced adapter is an independently written/altered integration;
// see vendor/re4_tweaks-LICENSE.txt and docs/re4-research.md for provenance.

namespace re4craft { namespace {
using NativeDraw=void(__cdecl*)(void*);
NativeDraw originalDraw{};GameAccess* displayOwner{};
bool readable(uintptr_t address, size_t count, bool write = false) {
    if (!address || count > UINTPTR_MAX - address) return false;
    uintptr_t end = address + count;
    while (address < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD p = mbi.Protect & 0xff;
        if (write && p != PAGE_READWRITE && p != PAGE_WRITECOPY && p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY) return false;
        if (!write && p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY && p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= address) return false;
        address = next < end ? next : end;
    }
    return true;
}
bool copyMemory(uintptr_t address, void* destination, size_t size) {
    // SEH-only read: see hookdetail::copy. Writes still use readable().
    if (address < 0x10000 || size > UINTPTR_MAX - address) return false;
    __try { std::memcpy(destination, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool storeFlag(uintptr_t address, bool hidden) {
    if (!readable(address, sizeof(uint32_t), true)) return false;
    __try {
        auto value = reinterpret_cast<volatile LONG*>(address);
        LONG current = *value;
        for (;;) {
            LONG changed = static_cast<LONG>(playerDisplayFlags(static_cast<uint32_t>(current),hidden));
            LONG previous = InterlockedCompareExchange(value, changed, current);
            if (previous == current) return true;
            current = previous;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool read(uintptr_t p, T& out) { return copyMemory(p, &out, sizeof(out)); }
template<class T> void field(const uint8_t* bytes,size_t offset,T& out){std::memcpy(&out,bytes+offset,sizeof(out));}
bool finiteVec(const Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x) < 1e8f && std::abs(p.y) < 1e8f && std::abs(p.z) < 1e8f; }
void cameraFields(const uint8_t* bytes,NativeSnapshot& s){
    std::memcpy(s.view,bytes,sizeof(s.view));std::memcpy(s.projection,bytes+0x34,sizeof(s.projection));
    field(bytes,0x74,s.cameraPosition);field(bytes,0x80,s.cameraTarget);field(bytes,0x90,s.fovDegrees);
}
bool validCamera(const NativeSnapshot& s){
    if(!finiteVec(s.cameraPosition)||!finiteVec(s.cameraTarget)||!std::isfinite(s.fovDegrees)||s.fovDegrees<=0||s.fovDegrees>=179)return false;
    for(const auto& row:s.view)for(float value:row)if(!std::isfinite(value))return false;
    for(const auto& row:s.projection)for(float value:row)if(!std::isfinite(value))return false;
    return true;
}
uintptr_t uniquePattern(const uint8_t* start, size_t length, const char* text) {
    std::vector<int> bytes;
    while (*text) {
        if (*text == ' ') { ++text; continue; }
        if (*text == '?') { bytes.push_back(-1); while (*text == '?') ++text; continue; }
        auto digit=[](char c) -> int { if (c>='0' && c<='9') return c-'0'; if (c>='a' && c<='f') return c-'a'+10; if (c>='A' && c<='F') return c-'A'+10; return -1; };
        int high=digit(text[0]),low=text[0]?digit(text[1]):-1;
        if (high<0 || low<0) return 0;
        bytes.push_back(16*high+low); text += 2;
    }
    if (bytes.empty() || length < bytes.size()) return 0;
    uintptr_t match{};
    for (size_t i = 0; i <= length - bytes.size(); ++i) {
        bool equal = true;
        for (size_t n = 0; n < bytes.size(); ++n) if (bytes[n] >= 0 && start[i+n] != bytes[n]) { equal = false; break; }
        if (equal) { if (match) return 0; match = reinterpret_cast<uintptr_t>(start+i); }
    }
    return match;
}
uintptr_t operand(uintptr_t p) { uint32_t value{}; return read(p, value) ? value : 0; }
}

bool GameAccess::initialize(LogFn log) {
    if(initialized_&&displayOwner==this)return true;
    restore(); initialized_ = false;
    auto base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    if (!copyMemory(reinterpret_cast<uintptr_t>(base), &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) return false;
    IMAGE_NT_HEADERS32 nt{};
    if (!copyMemory(reinterpret_cast<uintptr_t>(base)+dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const uint8_t* code{}; size_t size{};
    uintptr_t sections = reinterpret_cast<uintptr_t>(base)+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned n=0; n<nt.FileHeader.NumberOfSections; ++n) {
        IMAGE_SECTION_HEADER section{};
        if (!read(sections+n*sizeof(section), section)) return false;
        if (!std::memcmp(section.Name, ".text", 5)) { code = base+section.VirtualAddress; size = section.Misc.VirtualSize; break; }
    }
    if (!code || !readable(reinterpret_cast<uintptr_t>(code), size)) return false;
    auto find = [&](const char* pattern) { return uniquePattern(code, size, pattern); };
    uintptr_t versionMatch = find("68 ? ? ? ? 8B C8 E8 ? ? ? ? DC 2D ? ? ? ? A1 ? ? ? ? DC 25");
    char version[6]{};
    if (!versionMatch || !copyMemory(operand(versionMatch+1), version, 6) || (std::memcmp(version,"1.0.6",5) && std::memcmp(version,"1.1.0",5))) { if (log) log("Unsupported RE4 executable; bridge memory writes disabled."); return false; }
    uintptr_t globalMatch = find("A1 ? ? ? ? B9 FF FF FF 7F 21 48 ? A1");
    uintptr_t playerMatch = find("A1 ? ? ? ? D8 CC D8 C9 D8 CA D9 5D ? D9 45 ?");
    uintptr_t onMatch = find("A1 ? ? ? ? 33 C9 83 E0 ? 83 3D");
    uintptr_t triggerMatch = find("A1 ? ? ? ? 25 ? ? ? ? 0B C1 74 ? 88 0D");
    if (!globalMatch || !playerMatch || !onMatch || !triggerMatch) { if (log) log("A RE4 signature was missing or nonunique; bridge memory writes disabled."); return false; }
    globalSlot_ = operand(globalMatch+1); playerSlot_ = operand(playerMatch+1);
    keyOn_ = operand(onMatch+1); keyTrigger_ = operand(triggerMatch+1);
    if (!readable(globalSlot_,4) || !readable(playerSlot_,4) || !readable(keyOn_,8) || !readable(keyTrigger_,8)) return false;
    // Hash-pinned offline disassembly proves the unique cdecl queue gate takes
    // one model pointer. Its two operands must also be our scanned pPL/pG slots.
    // Refreshing here precedes native model/depth submission even when a
    // camera/script path has cleared DPF_PL since the previous EndScene.
    uintptr_t draw=find("55 8B EC 8B 45 08 8B 48 04 83 E1 03 80 F9 03 75 ? 3B 05 ? ? ? ? 75 ? 8B 15 ? ? ? ? F7 42 58 00 00 00 40");
    if(!draw||operand(draw+19)!=playerSlot_||operand(draw+27)!=globalSlot_||displayOwner){if(log)log("Native player queue gate missing/altered; early hide ownership unavailable.");return false;}
    auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    if(MH_CreateHook(reinterpret_cast<void*>(draw),reinterpret_cast<void*>(&GameAccess::nativeDrawHook),reinterpret_cast<void**>(&originalDraw))!=MH_OK)return false;
    displayOwner=this;log_=log;
    if(MH_EnableHook(reinterpret_cast<void*>(draw))!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(draw));displayOwner=nullptr;return false;}
    initialized_ = true;
    if (log) { char message[160]{}; std::snprintf(message,sizeof(message),"RE4 %.5s signatures resolved: global slot %08X, player slot %08X.",version,static_cast<unsigned>(globalSlot_),static_cast<unsigned>(playerSlot_)); log(message); }
    if(log){char message[160]{};std::snprintf(message,sizeof(message),"Native player pre-draw hide gate installed at %08X (owned DPF_PL, 250 ms heartbeat).",unsigned(draw));log(message);}
    return true;
}

NativeSnapshot GameAccess::sample() const {
    NativeSnapshot s{};
    if (!initialized_) return s;
    uint32_t global{}, player{};
    if (!read(globalSlot_,global) || !read(playerSlot_,player) || !global || !player) return s;
    s.globalAddress=global; s.playerAddress=player;
    // The SDK defines contiguous cCoord/GLOBAL_WK fields. Validate each small
    // range once, then extract the same fields, rather than invoking the kernel
    // once for every scalar and camera matrix in each projection/model callback.
    uint8_t playerFields[0xb0]{},earlyGlobal[0x174]{},lateGlobal[0x74]{};
    uint32_t vtable{};
    if(!copyMemory(player,playerFields,sizeof(playerFields)))return s;
    field(playerFields,0,vtable);field(playerFields,4,s.playerBeFlags);field(playerFields,0x94,s.position);field(playerFields,0xa0,s.angles);
    if(vtable<0x10000||!copyMemory(global,earlyGlobal,sizeof(earlyGlobal))||!copyMemory(global+0x4fac,lateGlobal,sizeof(lateGlobal)))return s;
    field(earlyGlobal,0x20,s.routine);field(earlyGlobal,0x170,s.stopFlags);field(earlyGlobal,0x70,s.deltaTime);cameraFields(earlyGlobal+0xa4,s);
    field(lateGlobal,0,s.room);field(lateGlobal,8,s.health);field(lateGlobal,10,s.maxHealth);field(lateGlobal,0x70,s.statusFlags);
    field(lateGlobal,0x1c,s.character);
    if(keyTrigger_==keyOn_+8){uint64_t keys[2]{};if(!copyMemory(keyOn_,keys,sizeof(keys)))return s;s.keyOn=keys[0];s.keyTrigger=keys[1];}
    else if(!read(keyOn_,s.keyOn)||!read(keyTrigger_,s.keyTrigger))return s;
    if(!finiteVec(s.position)||!finiteVec(s.angles)||!validCamera(s))return s;
    s.alive=(s.playerBeFlags&1)!=0 && (s.playerBeFlags&0x600)==0 && s.health>0;
    s.playing=s.routine==3;
    s.paused=(s.stopFlags & 0x92000000)!=0; // key, player, or controls processing stopped
    s.cutscene=(s.statusFlags & 0x1000)!=0; // STA_EVENT
    s.valid=true;
    return s;
}
NativeSnapshot GameAccess::protectionState()const {
    NativeSnapshot s{};uint32_t global{},player{},vtable{};
    if(!initialized_||!read(globalSlot_,global)||!read(playerSlot_,player)||!global||!player)return s;
    uint8_t actor[8]{},early[0x174]{},late[0x74]{};
    if(!copyMemory(player,actor,sizeof(actor))||!copyMemory(global,early,sizeof(early))
       ||!copyMemory(uintptr_t(global)+0x4fac,late,sizeof(late)))return s;
    field(actor,0,vtable);if(vtable<0x10000)return s;
    s.globalAddress=global;s.playerAddress=player;
    field(actor,4,s.playerBeFlags);field(early,0x20,s.routine);field(early,0x170,s.stopFlags);
    field(late,8,s.health);field(late,10,s.maxHealth);field(late,0x1c,s.character);field(late,0x70,s.statusFlags);
    s.valid=s.character<=6&&s.maxHealth>0&&s.health>=0;
    s.alive=(s.playerBeFlags&1)!=0&&(s.playerBeFlags&0x600)==0&&s.health>0;
    s.playing=s.routine==3;s.paused=(s.stopFlags&0x92000000)!=0;s.cutscene=(s.statusFlags&0x1000)!=0;
    return s;
}
GameLootContext GameAccess::lootContext()const {
    GameLootContext out{};uint32_t global{},after{};uint8_t late[0x2f]{};
    if(!initialized_||!read(globalSlot_,global)||!global
       ||!read(uintptr_t(global)+0x54,out.systemFlags)
       ||!copyMemory(uintptr_t(global)+0x4f9a,late,sizeof(late))
       ||!read(globalSlot_,after)||after!=global)return {};
    field(late,0,out.chapter);field(late,0x2e,out.character);
    out.valid=true;return out;
}
NativeTraversalState GameAccess::traversalState()const {
    NativeTraversalState out{};uint32_t global{},player{},afterGlobal{},afterPlayer{},flags{},vtable{};
    uint8_t actor[0x104]{},early[0x24]{},late[0xc]{};uint8_t routine{};int16_t hp{};
    if(!initialized_||!read(globalSlot_,global)||!read(playerSlot_,player)||!global||!player
       ||!copyMemory(player,actor,sizeof(actor))||!copyMemory(global,early,sizeof(early))
       ||!copyMemory(uintptr_t(global)+0x4fac,late,sizeof(late))
       ||!read(globalSlot_,afterGlobal)||afterGlobal!=global
       ||!read(playerSlot_,afterPlayer)||afterPlayer!=player)return out;
    field(actor,0,vtable);field(actor,4,flags);field(actor,0xf8,out.playerGuid);
    field(actor,0x94,out.position);field(actor,0xa0,out.angles);
    field(early,0x20,routine);field(late,0,out.room);field(late,8,hp);
    if(vtable<0x10000||!finiteVec(out.position)||!finiteVec(out.angles))return {};
    out.globalAddress=global;out.playerAddress=player;
    out.alive=(flags&1)!=0&&(flags&0x600)==0&&hp>0;out.playing=routine==3;
    out.valid=true;return out;
}
bool GameAccess::readCamera(NativeSnapshot& inout)const {
    uint32_t global{};uint8_t bytes[0x94]{};
    if(!initialized_||!read(globalSlot_,global)||!global||!copyMemory(global+0xa4,bytes,sizeof(bytes)))return false;
    NativeSnapshot fresh=inout;fresh.globalAddress=global;cameraFields(bytes,fresh);if(!validCamera(fresh))return false;
    inout=fresh;return true;
}
uintptr_t GameAccess::currentPlayerAddress()const {uint32_t player{};return initialized_&&read(playerSlot_,player)?player:0;}
uint8_t GameAccess::currentPlayerCharacter()const {
    uint32_t global{};uint8_t character=0xff;
    if(initialized_&&read(globalSlot_,global)&&global)read(uintptr_t(global)+0x4fc8,character);
    return character;
}

bool GameAccess::setReplacementVisible(bool visible) {
    std::lock_guard lock(displayMutex_);
    if (!visible) { restoreDisplayLocked(); return true; }
    NativeSnapshot s=sample();
    if (!s.canReplace()) { restoreDisplayLocked(); return false; }
    if (ownsDisplayBit_ && hiddenGlobal_ != s.globalAddress) restoreDisplayLocked();
    uint32_t flags{};
    if (!read(s.globalAddress+0x58,flags)) return false;
    if (!ownsDisplayBit_) { hiddenGlobal_=s.globalAddress; originalHidden_=(flags&HidePlayerMask)!=0; ownsDisplayBit_=true; }
    hideHeartbeat_=GetTickCount64();
    return storeFlag(s.globalAddress+0x58,true);
}
void GameAccess::restoreDisplayLocked() {
    if (ownsDisplayBit_) {
        uint32_t currentGlobal{};
        // Do not touch a stale allocation after the game destroys/replaces pG.
        if (read(globalSlot_,currentGlobal) && currentGlobal==hiddenGlobal_) storeFlag(hiddenGlobal_+0x58,originalHidden_);
    }
    ownsDisplayBit_=false; hiddenGlobal_=0;hideHeartbeat_=0;
}
void GameAccess::restore() {std::lock_guard lock(displayMutex_);restoreDisplayLocked();}
void __cdecl GameAccess::nativeDrawHook(void* model){
    if(displayOwner)displayOwner->refreshReplacementBeforeDraw(model);
    originalDraw(model);
}
void GameAccess::refreshReplacementBeforeDraw(void* model){
    // Unrelated actors need only one pointer read, with no camera sampling.
    uint32_t player{};if(!read(playerSlot_,player)||reinterpret_cast<uintptr_t>(model)!=player)return;
    bool report{};uint32_t resetCount{};
    {
        std::lock_guard lock(displayMutex_);if(!ownsDisplayBit_)return;
        const uint64_t now=GetTickCount64();
        if(!hideHeartbeat_||now-hideHeartbeat_>250){restoreDisplayLocked();return;}
        auto native=sample();
        if(!canRefreshPlayerHide(ownsDisplayBit_,hideHeartbeat_,now,native.canReplace(),native.globalAddress==hiddenGlobal_)){restoreDisplayLocked();return;}
        uint32_t flags{};if(!read(hiddenGlobal_+0x58,flags)){restoreDisplayLocked();return;}
        if(!(flags&HidePlayerMask)){
            ++hideResetCount_;
            report=log_&&hideResetLogs_<32&&(hideResetCount_<=4||now-lastHideResetLog_>=5000);
            if(report){++hideResetLogs_;lastHideResetLog_=now;resetCount=hideResetCount_;}
        }
        if(refreshOwnedPlayerDisplayFlags(flags,ownsDisplayBit_)!=flags)storeFlag(hiddenGlobal_+0x58,true);
    }
    if(report){char message[192]{};std::snprintf(message,sizeof(message),"Native player hide bit refreshed before queue draw: native resets=%u; original owned value retained.",resetCount);log_(message);}
}
}
