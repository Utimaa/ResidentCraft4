#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "collision_export.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

// SDK layout/signature provenance: nipkownix/re4_tweaks atari.h,
// cManager.h and DebugDisplay.cpp. See vendor/re4_tweaks-LICENSE.txt.
namespace re4craft { namespace {
constexpr uint32_t MaxEntries=4096;
constexpr uint32_t HardMaxTriangles=200000;
constexpr size_t HardMaxReadBytes=64u*1024u*1024u;
bool copyMemory(uintptr_t address,void* destination,size_t bytes) {
    if (!address || bytes>UINTPTR_MAX-address) return false;
    const uintptr_t end=address+bytes;
    for (uintptr_t cursor=address;cursor<end;) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor),&mbi,sizeof(mbi)) || mbi.State!=MEM_COMMIT || (mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
        DWORD p=mbi.Protect&0xff;
        if (p!=PAGE_READONLY && p!=PAGE_READWRITE && p!=PAGE_WRITECOPY && p!=PAGE_EXECUTE_READ && p!=PAGE_EXECUTE_READWRITE && p!=PAGE_EXECUTE_WRITECOPY) return false;
        uintptr_t next=reinterpret_cast<uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;
        if (next<=cursor) return false;
        cursor=std::min(next,end);
    }
    __try { std::memcpy(destination,reinterpret_cast<void*>(address),bytes); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T,size_t N> T field(const std::array<uint8_t,N>& raw,size_t offset) { T value{}; if (offset<=N && sizeof(T)<=N-offset) std::memcpy(&value,raw.data()+offset,sizeof(T)); return value; }
template<class T> bool read(uintptr_t address,T& out) { return copyMemory(address,&out,sizeof(out)); }
bool finite(const Vec3& v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::abs(v.x)<1e8f&&std::abs(v.y)<1e8f&&std::abs(v.z)<1e8f; }
bool optionsValid(const CollisionReadOptions& o) { return finite(o.center)&&std::isfinite(o.radius)&&o.radius>0&&o.radius<=10000000&&o.maxTriangles>0&&o.maxTriangles<=HardMaxTriangles&&o.maxReadBytes>=0xc0&&o.maxReadBytes<=HardMaxReadBytes; }
Vec3 transform(const float m[3][4],const Vec3& p) { return {m[0][0]*p.x+m[0][1]*p.y+m[0][2]*p.z+m[0][3],m[1][0]*p.x+m[1][1]*p.y+m[1][2]*p.z+m[1][3],m[2][0]*p.x+m[2][1]*p.y+m[2][2]*p.z+m[2][3]}; }
uint64_t sourceKey(uintptr_t entry,uint32_t vp,uint32_t pp,uint16_t vn,uint16_t pn){
    uint64_t key=1469598103934665603ULL;for(uint64_t value:{uint64_t(entry),uint64_t(vp),uint64_t(pp),uint64_t(vn),uint64_t(pn)}){key^=value;key*=1099511628211ULL;}return key?key:1;
}
bool relativeMatrix(const float current[3][4],const float base[3][4],float units,float ySign,float out[3][4]){
    const double a=base[0][0],b=base[0][1],c=base[0][2],d=base[1][0],e=base[1][1],f=base[1][2],g=base[2][0],h=base[2][1],i=base[2][2];
    const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);if(!std::isfinite(determinant)||std::abs(determinant)<1e-8)return false;
    const double inverse[3][3]{{(e*i-f*h)/determinant,(c*h-b*i)/determinant,(b*f-c*e)/determinant},
        {(f*g-d*i)/determinant,(a*i-c*g)/determinant,(c*d-a*f)/determinant},
        {(d*h-e*g)/determinant,(b*g-a*h)/determinant,(a*e-b*d)/determinant}};
    const double scales[]{double(units),double(units*ySign),double(units)};
    for(size_t row=0;row<3;++row){double translation=current[row][3];for(size_t col=0;col<3;++col){double v{};for(size_t k=0;k<3;++k)v+=current[row][k]*inverse[k][col];translation-=v*base[col][3];out[row][col]=float(v*scales[col]/scales[row]);if(!std::isfinite(out[row][col])||std::abs(out[row][col])>4)return false;}out[row][3]=float(translation/scales[row]);if(!std::isfinite(out[row][3])||std::abs(out[row][3])>30000000)return false;}return true;
}
bool intersects(const CollisionTriangle& t,const CollisionReadOptions& o) {
    return std::min({t.a.x,t.b.x,t.c.x})<=o.center.x+o.radius && std::max({t.a.x,t.b.x,t.c.x})>=o.center.x-o.radius &&
        std::min({t.a.y,t.b.y,t.c.y})<=o.center.y+o.radius && std::max({t.a.y,t.b.y,t.c.y})>=o.center.y-o.radius &&
        std::min({t.a.z,t.b.z,t.c.z})<=o.center.z+o.radius && std::max({t.a.z,t.b.z,t.c.z})>=o.center.z-o.radius;
}
// SAT's source signature is intentionally repeated at several actor callers.
// It is accepted only when EVERY matching immediate resolves to the same object.
uintptr_t agreedManager(const uint8_t* start,size_t length,const std::vector<int>& pattern,size_t operand) {
    if (length<pattern.size()+4 || operand+4>pattern.size()+4) return 0;
    uintptr_t result{};
    for (size_t offset=0;offset+pattern.size()+4<=length;++offset) {
        bool match=true;
        for (size_t n=0;n<pattern.size();++n) if (pattern[n]>=0 && start[offset+n]!=pattern[n]) { match=false; break; }
        if (!match) continue;
        uint32_t address{}; std::memcpy(&address,start+offset+operand,4);
        if (!address || (result && result!=address)) return 0;
        result=address;
    }
    return result;
}
}

bool CollisionExporter::initialize(LogFn log) {
    satManager_=eatManager_=0;
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 nt{};
    if (!copyMemory(reinterpret_cast<uintptr_t>(base),&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || !copyMemory(reinterpret_cast<uintptr_t>(base)+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 || nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const uint8_t* code{};size_t size{};
    uintptr_t sections=reinterpret_cast<uintptr_t>(base)+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned i=0;i<nt.FileHeader.NumberOfSections;++i) {
        IMAGE_SECTION_HEADER section{}; if (!read(sections+i*sizeof(section),section)) return false;
        if (!std::memcmp(section.Name,".text",5)) { code=base+section.VirtualAddress;size=section.Misc.VirtualSize;break; }
    }
    if (!code || !size) return false;
    std::vector<uint8_t> text(size); if (!copyMemory(reinterpret_cast<uintptr_t>(code),text.data(),text.size())) return false;
    satManager_=agreedManager(text.data(),text.size(),{0x8d,0x8e,0xb4,0x02,0,0,0xe8,-1,-1,-1,-1,0x6a,0,0x56,0xb9},15);
    eatManager_=agreedManager(text.data(),text.size(),{0x6a,0,0x53,0x56,0x50,0x51,0xb9,-1,-1,-1,-1,0xe8},7);
    std::array<uint8_t,0x20> manager{};
    if (!satManager_ || !read(satManager_,manager)) { satManager_=eatManager_=0;return false; }
    if (eatManager_ && !read(eatManager_,manager)) eatManager_=0;
    if (log) { char message[128]{}; std::snprintf(message,sizeof(message),"Experimental collision managers resolved SAT=%08X EAT=%08X (read only).",static_cast<unsigned>(satManager_),static_cast<unsigned>(eatManager_));log(message); }
    return true;
}

CollisionSnapshot CollisionExporter::captureManager(uintptr_t address,const CollisionReadOptions& o,uint8_t kind) {
    CollisionSnapshot out{};
    if (!address || !optionsValid(o) || kind>1) return out;
    std::array<uint8_t,0x20> header{};
    if (!read(address,header)) return out;
    const uint32_t array=field<uint32_t>(header,4),count=field<uint32_t>(header,8),stride=field<uint32_t>(header,0xc);
    if (count>MaxEntries || (count && (!array || stride!=0xc0 || static_cast<uint64_t>(array)+static_cast<uint64_t>(count)*stride>UINTPTR_MAX))) return out;
    out.valid=true;out.complete=true;out.bytesRead=header.size();
    auto budgetRead=[&](uintptr_t p,void* dst,size_t bytes) {
        if (bytes>o.maxReadBytes-out.bytesRead) { out.complete=false;out.truncated=true;return false; }
        if (!copyMemory(p,dst,bytes)) { out.complete=false;return false; }
        out.bytesRead+=bytes;return true;
    };
    for (uint32_t index=0;index<count;++index) {
        std::array<uint8_t,0xc0> sat{};
        uintptr_t entry=static_cast<uintptr_t>(array)+index*stride;
        if (!budgetRead(entry,sat.data(),sat.size())) { ++out.rejectedEntries;if(out.truncated)break;continue; }
        if ((field<uint32_t>(sat,4)&0x201)!=1 || !(sat[0x2a]&4)) continue;
        ++out.activeEntries;
        const uint32_t vp=field<uint32_t>(sat,0xc),pp=field<uint32_t>(sat,0x18);
        const uint16_t vn=field<uint16_t>(sat,0x1c),pn=field<uint16_t>(sat,0x1e),floors=field<uint16_t>(sat,0x20),slopes=field<uint16_t>(sat,0x22),walls=field<uint16_t>(sat,0x24);
        float matrix[3][4]{};std::memcpy(matrix,sat.data()+0x60,sizeof(matrix));
        bool matrixValid=true; for(auto& row:matrix)for(float v:row)if(!std::isfinite(v)||std::abs(v)>1e8f)matrixValid=false;
        if (!matrixValid || !vp || !pp || vn<3 || !pn || floors>pn || slopes>pn-floors || walls>pn) { ++out.rejectedEntries;out.complete=false;continue; }
        std::vector<Vec3> vertices(vn);std::vector<std::array<uint16_t,10>> polygons(pn);
        if (!budgetRead(vp,vertices.data(),vertices.size()*sizeof(Vec3)) || !budgetRead(pp,polygons.data(),polygons.size()*sizeof(polygons[0]))) { ++out.rejectedEntries;if(out.truncated)break;continue; }
        bool entryValid=true;
        for(const auto& v:vertices)if(!finite(v)){entryValid=false;break;}
        for(const auto& p:polygons)if(p[0]>=vn||p[1]>=vn||p[2]>=vn){entryValid=false;break;}
        std::array<uint8_t,0xc0> check{};
        if (!budgetRead(entry,check.data(),check.size())) { ++out.rejectedEntries;if(out.truncated)break;continue; }
        // A room unload / moving instance during capture invalidates this entry.
        if (sat!=check) entryValid=false;
        if (!entryValid) { ++out.rejectedEntries;out.complete=false;continue; }
        const size_t firstTriangle=out.triangles.size();
        const auto key=sourceKey(entry,vp,pp,vn,pn);
        for(uint32_t poly=0;poly<pn;++poly) {
            const auto& p=polygons[poly];CollisionTriangle t{};
            t.a=transform(matrix,vertices[p[0]]);t.b=transform(matrix,vertices[p[1]]);t.c=transform(matrix,vertices[p[2]]);
            if(!finite(t.a)||!finite(t.b)||!finite(t.c)){out.complete=false;continue;}
            if(!intersects(t,o))continue;
            double ux=double(t.b.x)-t.a.x,uy=double(t.b.y)-t.a.y,uz=double(t.b.z)-t.a.z;
            double vx=double(t.c.x)-t.a.x,vy=double(t.c.y)-t.a.y,vz=double(t.c.z)-t.a.z;
            double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,length=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(length<1e-8)continue;
            t.normal={static_cast<float>(nx/length),static_cast<float>(ny/length),static_cast<float>(nz/length)};
            t.attributes=(uint32_t(p[8])<<16)|p[9];t.sourceIndex=index;t.polygonIndex=poly;t.managerKind=kind;t.sourceKey=key;
            if(poly<floors)t.surface=CollisionSurface::Floor;else if(poly<uint32_t(floors)+slopes)t.surface=CollisionSurface::Slope;else if(walls && poly>=uint32_t(pn)-walls)t.surface=CollisionSurface::Wall;
            if(out.triangles.size()>=o.maxTriangles){out.complete=false;out.truncated=true;break;}
            out.triangles.push_back(t);
        }
        if(out.triangles.size()!=firstTriangle){CollisionInstance instance{};instance.key=key;instance.address=entry;instance.vertices=vp;instance.polygons=pp;instance.vertexCount=vn;instance.polygonCount=pn;std::memcpy(instance.matrix,matrix,sizeof(matrix));out.instances.push_back(instance);}
        if(out.truncated)break;
    }
    std::array<uint8_t,0x20> check{};
    if (!read(address,check) || header!=check) { out.valid=false;out.complete=false;out.triangles.clear(); }
    return out;
}

bool CollisionExporter::captureTransforms(uintptr_t address,const std::vector<CollisionInstance>& baseline,float units,float ySign,std::vector<CollisionTransform>& output){
    output.clear();if(!std::isfinite(units)||units<=0||!std::isfinite(ySign)||std::abs(ySign)!=1||baseline.size()>MaxEntries)return false;
    std::array<uint8_t,0x20> header{},check{};if(!read(address,header))return false;
    const uint32_t array=field<uint32_t>(header,4),count=field<uint32_t>(header,8),stride=field<uint32_t>(header,0xc);
    if(count>MaxEntries||stride!=0xc0||(count&&(!array||uint64_t(array)+uint64_t(count)*stride>UINTPTR_MAX)))return false;
    std::vector<std::array<uint8_t,0xc0>> entries(count);if(count&&!copyMemory(array,entries.data(),entries.size()*0xc0))return false;
    if(!read(address,check)||header!=check)return false;
    for(const auto& base:baseline){CollisionTransform result{};result.key=base.key;
        bool valid=base.address>=array&&(base.address-array)%stride==0&&(base.address-array)/stride<count;
        float matrix[3][4]{};
        if(valid){const auto& sat=entries[(base.address-array)/stride];valid=(field<uint32_t>(sat,4)&0x201)==1&&(sat[0x2a]&4)
            &&field<uint32_t>(sat,0xc)==base.vertices&&field<uint32_t>(sat,0x18)==base.polygons&&field<uint16_t>(sat,0x1c)==base.vertexCount&&field<uint16_t>(sat,0x1e)==base.polygonCount;
            std::memcpy(matrix,sat.data()+0x60,sizeof(matrix));}
        if(!valid||!relativeMatrix(matrix,base.matrix,units,ySign,result.matrix)){result.flags=1;output.push_back(result);continue;}
        if(std::memcmp(matrix,base.matrix,sizeof(matrix)))output.push_back(result);
    }
    return true;
}

CollisionSnapshot CollisionExporter::capture(const CollisionReadOptions& o) const {
    CollisionSnapshot out=captureManager(satManager_,o,0);
    if (!out.valid || !o.includeEat || !eatManager_ || out.truncated) return out;
    CollisionReadOptions remainder=o;
    if (out.triangles.size()>=o.maxTriangles || out.bytesRead>=o.maxReadBytes) { out.truncated=true;out.complete=false;return out; }
    remainder.maxTriangles-=static_cast<uint32_t>(out.triangles.size());remainder.maxReadBytes-=out.bytesRead;
    auto extra=captureManager(eatManager_,remainder,1);
    out.complete &= extra.valid&&extra.complete;out.truncated |= extra.truncated;
    out.activeEntries+=extra.activeEntries;out.rejectedEntries+=extra.rejectedEntries;out.bytesRead+=extra.bytesRead;
    out.triangles.insert(out.triangles.end(),extra.triangles.begin(),extra.triangles.end());return out;
}
}
