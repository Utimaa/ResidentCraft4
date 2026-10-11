#pragma once
#include <cstdint>

namespace re4craft {
// Observe an existing genuine attachment before the native binding call.
// An unknown/missing depth buffer supplies no world-occlusion evidence.
inline bool worldDepthBindingBoundary(bool enabled,bool sameDevice,bool drawingBridge,bool suppressed,
                                      bool readable,const void* current,const void* next) {
    return enabled&&sameDevice&&!drawingBridge&&!suppressed&&readable&&current&&current!=next;
}
// Per actual D3D scene, rather than per game update or queued GX call.
// Kept independent of Direct3D so ordering and repeated-boundary behavior can
// be tested without assuming that an attached depth surface contains a world.
struct WorldDepthPhase {
    bool perspective{}, delivered{};
    uint32_t nativeDraws{};
    void begin() { perspective=delivered=false;nativeDraws=0; }
    bool continues(bool exactSameCamera) const { return perspective&&!delivered&&exactSameCamera; }
    void arm(bool mainCamera) { perspective=mainCamera;nativeDraws=0; }
    void nativeDraw() { if(perspective&&!delivered)++nativeDraws; }
    bool boundary() {
        bool flush=perspective&&!delivered&&nativeDraws>0;
        perspective=false;nativeDraws=0;
        if(flush)delivered=true;
        return flush;
    }
};
}
