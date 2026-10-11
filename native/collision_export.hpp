#pragma once
#include "game_access.hpp"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace re4craft {
enum class CollisionSurface : uint8_t { Floor=0, Slope=1, Wall=2, Other=3 };
struct CollisionTriangle {
    Vec3 a,b,c,normal;
    uint32_t attributes{},sourceIndex{},polygonIndex{};
    CollisionSurface surface{CollisionSurface::Other};
    uint8_t managerKind{}; // 0 SAT, 1 EAT (diagnostic; semantics unverified)
};
struct CollisionReadOptions {
    Vec3 center{}; // native RE4 world units
    float radius{3000.f}; // axis-aligned query extent, NOT triangle-center distance
    uint32_t maxTriangles{50000};
    size_t maxReadBytes{32u*1024u*1024u};
    bool includeEat{};
};
struct CollisionSnapshot {
    bool valid{},complete{},truncated{};
    uint32_t activeEntries{},rejectedEntries{};
    size_t bytesRead{};
    std::vector<CollisionTriangle> triangles;
};

// Experimental read-only extraction. Call on the native game/render thread
// after GameAccess::sample().canReplace(); this API never changes game state.
// Protocol v3 sends complete, fresh bounded SAT neighborhoods for guest
// movement and targeting. Decoding terrain does not cover every native action.
class CollisionExporter {
public:
    bool initialize(LogFn log=nullptr);
    CollisionSnapshot capture(const CollisionReadOptions& options) const;
    uintptr_t satManager() const { return satManager_; }
    uintptr_t eatManager() const { return eatManager_; }
    // Explicit-address decoder also supports synthetic, process-local fixtures.
    static CollisionSnapshot captureManager(uintptr_t address,const CollisionReadOptions& options,uint8_t managerKind=0);
private:
    uintptr_t satManager_{},eatManager_{};
};
}
