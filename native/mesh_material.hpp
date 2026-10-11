#pragma once
#include <cstdint>

namespace re4craft {
// Concrete vanilla pipeline properties, independent of packet category.
// Bit1 retains the existing double-sided marker; bit2 alpha, bit4 crumbling,
// bit8 vanilla particle alpha threshold/depth behavior, bit16 vanilla TNT
// overlay(U15,V10): whiteRGB, source/base weight63/255, alpha unchanged.
constexpr bool validMeshMaterial(uint32_t flags){return !(flags&~31U)&&!((flags&4U)&&(flags&(2U|8U|16U)));}
constexpr bool meshMaterialDepthWrite(uint32_t flags){return !(flags&4U)&&(!(flags&2U)||(flags&8U));}
constexpr uint32_t meshMaterialAlphaReference(uint32_t flags){return (flags&(4U|8U))?25U:(flags&2U)?0U:127U;}
}
