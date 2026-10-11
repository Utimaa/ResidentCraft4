#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace re4craft {
struct D3DDeviceSnapshot {
    uintptr_t device{},tableAddress{};
    std::array<void*,85> table{};
};

/** Copy the published native table without calling or retaining a COM object.
 * Ops::read(address,destination,size) must safely reject inaccessible memory;
 * Ops::executable(pointer) must reject nonexecutable method addresses.
 * The copied table uses this process's pointer width (RE4 is native x86).
 * Consumers use the snapshot table and must not dereference device afterward.
 */
template<class Ops> bool acquireD3DDeviceSnapshot(uintptr_t deviceSlot,Ops& ops,D3DDeviceSnapshot& output) {
    output={};D3DDeviceSnapshot candidate{};
    if(!deviceSlot||!ops.read(deviceSlot,&candidate.device,sizeof(candidate.device))||!candidate.device
        ||!ops.read(candidate.device,&candidate.tableAddress,sizeof(candidate.tableAddress))||!candidate.tableAddress
        ||!ops.read(candidate.tableAddress,candidate.table.data(),sizeof(candidate.table)))return false;
    constexpr std::array<size_t,11> required{16,17,37,39,41,42,43,81,82,83,84};
    for(size_t index:required)if(!candidate.table[index]||!ops.executable(candidate.table[index]))return false;
    uintptr_t confirmedTable{},confirmedDevice{};
    if(!ops.read(candidate.device,&confirmedTable,sizeof(confirmedTable))||confirmedTable!=candidate.tableAddress
        ||!ops.read(deviceSlot,&confirmedDevice,sizeof(confirmedDevice))||confirmedDevice!=candidate.device)return false;
    output=candidate;return true;
}
}
