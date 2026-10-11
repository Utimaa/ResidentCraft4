#pragma once
#include "game_access.hpp"
#include <vector>

namespace re4craft {
// Positive kinds from pinned re4_tweaks Game.cpp EmNames. Mixed model labels
// require the native class proof below; unknown and neutral actors stay false.
constexpr bool knownNativeHostileKind(uint8_t kind) {
    switch(kind) {
    case 0x11:case 0x12:case 0x15:case 0x16:case 0x17:case 0x19:case 0x1a:case 0x1b:case 0x1c:case 0x1d:case 0x1f:case 0x20:
    case 0x22:case 0x25:case 0x2b:case 0x2c:case 0x2d:case 0x2f:case 0x31:case 0x32:case 0x35:case 0x36:case 0x38:case 0x39:
    case 0x3c:case 0x3f:case 0x42:case 0x43:case 0x44:return true;
    default:return false;
    }
}
namespace hostilitydetail {
// Immutable mapped-section snapshots keep research/fixtures independent of
// live process reads. Addresses in RTTI and code are logical x86 image VAs.
struct Section {uint32_t rva{},flags{};std::vector<uint8_t> bytes;};
struct Image {uint32_t base{},size{};std::vector<Section> sections;};
struct Tables {uintptr_t commonGanado{},merchant{},commonConstructor{},merchantConstructor{},baseConstructor{};};
bool resolve(const Image& image,Tables& tables);
constexpr bool commonGanadoActor(uint8_t kind,uintptr_t vtable,const Tables& tables) {
    return tables.commonGanado && tables.merchant && (kind==0x13||kind==0x14||kind==0x1e)
        && vtable==tables.commonGanado && vtable!=tables.merchant;
}
}
class NativeHostility {
public:
    bool initialize(LogFn log=nullptr);
    bool commonGanadoActor(uint8_t kind,uintptr_t vtable) const {
        return hostilitydetail::commonGanadoActor(kind,vtable,tables_);
    }
    bool merchantActor(uintptr_t vtable) const {return tables_.merchant && vtable==tables_.merchant;}
private:
    hostilitydetail::Tables tables_{};
};
}
