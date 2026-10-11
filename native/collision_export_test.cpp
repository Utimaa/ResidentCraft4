// Standalone x86 synthetic-memory fixture; no game process or files are touched.
#include "collision_export.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
template<class T,size_t N> void put(std::array<uint8_t,N>& memory,size_t offset,T value) { std::memcpy(memory.data()+offset,&value,sizeof(value)); }
uint32_t pointer(const void* p) { static_assert(sizeof(void*)==4,"Fixture must run as x86");return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
bool equal(float x,float y) { return std::abs(x-y)<1e-5f; }
}
int main() {
    using namespace re4craft;
    std::array<uint8_t,0x20> manager{};std::array<uint8_t,0xc0> sat{};
    Vec3 vertices[3]{{0,0,0},{0,0,1},{1,0,0}};
    std::array<uint16_t,10> polygons[2]{{0,1,2,0,0,0,0,0,0x1234,0xabcd},{0,1,2,0,0,0,0,0,0,0}};
    put(manager,4,pointer(sat.data()));put(manager,8,uint32_t(1));put(manager,0xc,uint32_t(0xc0));
    put(sat,4,uint32_t(1));put(sat,0xc,pointer(vertices));put(sat,0x18,pointer(polygons));
    put(sat,0x1c,uint16_t(3));put(sat,0x1e,uint16_t(1));put(sat,0x20,uint16_t(1));sat[0x2a]=4;
    const float matrix[3][4]{{1,0,0,10},{0,1,0,20},{0,0,1,30}};std::memcpy(sat.data()+0x60,matrix,sizeof(matrix));
    CollisionReadOptions options{};options.center={10,20,30};options.radius=1.f;
    auto capture=[&] { return CollisionExporter::captureManager(reinterpret_cast<uintptr_t>(manager.data()),options); };
    auto good=capture();
    if(!good.valid||!good.complete||good.triangles.size()!=1)return 1;
    const auto& t=good.triangles[0];
    if(!equal(t.a.x,10)||!equal(t.a.y,20)||!equal(t.a.z,30)||!equal(t.b.z,31)||!equal(t.c.x,11)||!equal(t.normal.y,1)||t.attributes!=0x1234abcd||t.surface!=CollisionSurface::Floor)return 2;
    options.center={1000,1000,1000};if(!capture().triangles.empty())return 3;options.center={10,20,30};
    polygons[0][2]=99;auto bad=capture();if(!bad.valid||bad.complete||bad.rejectedEntries!=1||!bad.triangles.empty())return 4;polygons[0][2]=2;
    options.maxReadBytes=0xc0;auto limited=capture();if(!limited.truncated||limited.complete)return 5;options.maxReadBytes=32*1024*1024;
    put(sat,0x1e,uint16_t(2));options.maxTriangles=1;auto clipped=capture();if(!clipped.truncated||clipped.complete||clipped.triangles.size()!=1)return 6;
    put(sat,0x1e,uint16_t(1));options.maxTriangles=50000;sat[0x2a]=0;auto disabled=capture();if(!disabled.valid||!disabled.complete||!disabled.triangles.empty())return 7;
    sat[0x2a]=4;put(manager,0xc,uint32_t(1));if(capture().valid)return 8;
    if(CollisionExporter::captureManager(1,options).valid)return 9;
    put(manager,0xc,uint32_t(0xc0));
    // A distant exact surface belongs to the requested 256-block neighborhood,
    // but not to the old12-block import. Do not inflate the actual triangles.
    const float distant[3][4]{{1,0,0,200000},{0,1,0,0},{0,0,1,0}};std::memcpy(sat.data()+0x60,distant,sizeof(distant));
    options.center={0,0,0};options.radius=12000;if(!capture().triangles.empty())return 10;
    options.radius=256000;auto far=capture();if(!far.valid||!far.complete||far.truncated||far.triangles.size()!=1||!equal(far.triangles[0].a.x,200000))return 11;
    options.radius=2560000;auto scaled=capture();if(!scaled.valid||!scaled.complete)return 12;
    options.radius=10000001;if(capture().valid)return 13;
    std::puts("RE4 collision extraction fixture passed: transform, attributes, active flags, bounds, distant256-block/scaled-unit coverage, invalid indices/pointers, read budget and truncation.");
    return 0;
}
