#pragma once
#include "hook_support.hpp"
#include <array>

namespace re4craft {
// Verified GX queue record: callback at0, matrix at8, kind at0x48.
// Copy the whole record once, retaining guarded memory checks while avoiding
// three VirtualQuery walks for every repeated native object projection.
inline bool readQueuedProjection(uintptr_t command, uintptr_t consumer,
                                 uint32_t kind, const float (&matrix)[4][4]) {
    std::array<uint8_t,0x4c> record{};
    if(!hookdetail::copy(command,record.data(),record.size()))return false;
    uint32_t actualConsumer{},actualKind{};
    std::memcpy(&actualConsumer,record.data(),4);
    std::memcpy(&actualKind,record.data()+0x48,4);
    return actualConsumer==consumer&&actualKind==kind
        &&std::memcmp(record.data()+8,matrix,sizeof(matrix))==0;
}
}
