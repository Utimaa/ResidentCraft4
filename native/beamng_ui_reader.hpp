#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace re4craft {
constexpr uint32_t UiMappingMagic=0x49553452,UiMappingVersion=2;
constexpr size_t UiMappingHeaderSize=64,UiMappingSlotHeaderSize=32;
constexpr size_t UiMappingCapacity=1920u*1080u*4u+16u;
constexpr size_t UiMappingStride=UiMappingSlotHeaderSize+UiMappingCapacity;
constexpr size_t UiMappingSize=UiMappingHeaderSize+2u*UiMappingStride;
constexpr uint64_t UiMappingMaxCounter=(uint64_t(1)<<53)-1;
constexpr uint64_t UiMappingMaxAge=500;
#pragma pack(push,1)
struct UiMappingHeader {
    uint32_t magic,version,owner,capacity;
    uint64_t creation,sequence;
    uint32_t producer,flags;
    uint64_t reserved[3];
};
struct UiMappingSlot {
    uint32_t lock,length;
    // Version 2 uses reserved as exact capture mode: 0 original play HUD,
    // 1 original owned vehicle picker. No Hud4/public wire bits are changed.
    uint64_t sequence,stamp,reserved;
};
#pragma pack(pop)
static_assert(sizeof(UiMappingHeader)==64&&sizeof(UiMappingSlot)==32,"Original UI mapping ABI");
struct UiCapture {std::vector<uint8_t> payload;uint64_t sequence{},stamp{};bool screenOpen{};};
enum class UiReadStatus {Frame,Empty,Unchanged,Inactive,Writing,Replaced,Header,Slot,Stale,Future,BeforeReset,Regressed,Hud};
// This bounded decoder is also exercised by the x86 read-only mapping fixture.
UiReadStatus readUiMapping(const volatile uint8_t* mapping,size_t size,uint32_t owner,uint64_t creation,
    uint64_t now,uint64_t last,uint64_t after,bool active,UiCapture& result);

class BeamngUiReader {
public:
    BeamngUiReader();
    ~BeamngUiReader();
    BeamngUiReader(const BeamngUiReader&)=delete;
    BeamngUiReader& operator=(const BeamngUiReader&)=delete;
    bool poll(uintptr_t connectedSocket,uint32_t epoch,bool active,uint64_t now,UiCapture& result);
    uint32_t owner() const;
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
