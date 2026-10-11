#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "renderer.hpp"
#include "mesh_material.hpp"
#include "vehicle_display_color.hpp"
#include "vehicle_daylight.hpp"
#include "vehicle_deformation.hpp"
#include "vehicle_texture_storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <cstdio>
#include <limits>
#include <sstream>
#include <deque>
#include <new>
#include <d3dcompiler.h>

namespace re4craft { namespace {
constexpr size_t MaxTextures = 256;
template<class T> void releaseStorage(std::vector<T>& values){std::vector<T>{}.swap(values);}
constexpr DWORD AvatarFVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr DWORD VehicleFVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2;
struct FixedVertex { float x,y,z,nx,ny,nz; uint32_t color; float u,v; };
struct FixedVehicleVertex { float x,y,z,nx,ny,nz; uint32_t color; float u,v,u2,v2; };
struct FixedMorphVehicleVertex {FixedVehicleVertex current;float x,y,z,nx,ny,nz;};
static_assert(sizeof(FixedVertex)==36);
static_assert(sizeof(FixedVehicleVertex)==44);
static_assert(sizeof(FixedMorphVehicleVertex)==68);
DWORD floatState(float value){DWORD bits{};std::memcpy(&bits,&value,sizeof(bits));return bits;}
float depthResolution(D3DFORMAT format){
    switch(format){case D3DFMT_D16:case D3DFMT_D16_LOCKABLE:return 1.f/65535.f;case D3DFMT_D15S1:return 1.f/32767.f;
        case D3DFMT_D32:return 1.f/4294967295.f;case D3DFMT_D32F_LOCKABLE:return 1.f/8388608.f;case D3DFMT_D24FS8:return 1.f/65536.f;
        default:return 1.f/16777215.f;}
}
bool finiteVertex(const Vertex& v) {
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.nx)&&std::isfinite(v.ny)&&std::isfinite(v.nz)&&std::isfinite(v.u)&&std::isfinite(v.v) && std::abs(v.x)<1e5f&&std::abs(v.y)<1e5f&&std::abs(v.z)<1e5f;
}
size_t triangleVertexCount(const RenderBatch& b){return b.indices.empty()?b.vertices.size():b.indices.size();}
size_t triangleVertexIndex(const RenderBatch& b,size_t corner){return b.indices.empty()?corner:b.indices[corner];}
struct ClipPoint { double x{},y{},z{},w{}; };
struct Bounds {Vec3 min{},max{};bool valid{};};
Vec3 placed(const Vertex& v,const RenderPlacement& p) {
    if(p.useOrientation){auto r=vehicleRotate(p.orientation,{v.x,v.y,v.z});return {p.origin.x+float(r.x)*p.worldUnitsPerBlock,p.origin.y+float(r.y)*p.worldUnitsPerBlock*p.ySign,p.origin.z+float(r.z)*p.worldUnitsPerBlock};}
    const float c=std::cos(p.yawRadians),s=std::sin(p.yawRadians),scale=p.worldUnitsPerBlock;
    return {p.origin.x+scale*(c*v.x+s*v.z),p.origin.y+scale*p.ySign*v.y,p.origin.z+scale*(-s*v.x+c*v.z)};
}
D3DMATRIX placementMatrix(const RenderPlacement& p){
    D3DMATRIX world{};const float scale=p.worldUnitsPerBlock;
    if(p.useOrientation){for(unsigned i=0;i<3;i++){VehicleVector basis{};if(i==0)basis.x=1;if(i==1)basis.y=1;if(i==2)basis.z=1;auto r=vehicleRotate(p.orientation,basis);world.m[i][0]=float(r.x)*scale;world.m[i][1]=float(r.y)*scale*p.ySign;world.m[i][2]=float(r.z)*scale;}}
    else {const float c=std::cos(p.yawRadians),s=std::sin(p.yawRadians);world.m[0][0]=scale*c;world.m[0][2]=-scale*s;world.m[1][1]=scale*p.ySign;world.m[2][0]=scale*s;world.m[2][2]=scale*c;}
    world.m[3][0]=p.origin.x;world.m[3][1]=p.origin.y;world.m[3][2]=p.origin.z;world.m[3][3]=1;return world;
}
bool validateBatches(const std::vector<RenderBatch>& batches,bool vehicle,Bounds& bounds){
    const bool indexed=std::any_of(batches.begin(),batches.end(),[](const auto& b){return !b.indices.empty();});
    if(indexed&&!vehicle)return false;
    if(batches.size()>(indexed?MaxVehicleBatches:MaxBatches))return false;size_t total{},indices{};
    for(const auto& b:batches){
        if(!validMeshMaterial(b.flags)||(b.indices.empty()&&b.vertices.size()%3)||b.vertices.size()>MaxVehicleVertices||total>MaxVehicleVertices-b.vertices.size()||
           (b.materialId&&(!vehicle||b.secondaryUV.size()!=b.vertices.size()))||(!b.secondaryUV.empty()&&b.secondaryUV.size()!=b.vertices.size())||
           (!b.sourceIndices.empty()&&b.sourceIndices.size()!=b.vertices.size()))return false;
        if(indexed){const size_t count=triangleVertexCount(b);if(count>MaxVehicleIndices-indices)return false;indices+=count;}
        if(!b.indices.empty()){
            if(!vehicle||!b.materialId||!b.sourceIndices.empty()||b.indices.size()%3)return false;
            for(uint32_t index:b.indices)if(index>=b.vertices.size())return false;
        }
        for(uint32_t index:b.sourceIndices)if(index>=MaxVertices)return false;
        total+=b.vertices.size();for(const auto& uv:b.secondaryUV)if(!std::isfinite(uv[0])||!std::isfinite(uv[1])||std::abs(uv[0])>1e6f||std::abs(uv[1])>1e6f)return false;
        for(const auto& v:b.vertices){if(!finiteVertex(v))return false;
            if(!bounds.valid){bounds.min=bounds.max={v.x,v.y,v.z};bounds.valid=true;}
            else {bounds.min.x=std::min(bounds.min.x,v.x);bounds.min.y=std::min(bounds.min.y,v.y);bounds.min.z=std::min(bounds.min.z,v.z);bounds.max.x=std::max(bounds.max.x,v.x);bounds.max.y=std::max(bounds.max.y,v.y);bounds.max.z=std::max(bounds.max.z,v.z);}}
    }return true;
}
ClipPoint project(const Vec3& world,const NativeSnapshot& native) {
    double camera[4]{0,0,0,1};
    const double point[4]{world.x,world.y,world.z,1};
    for(int row=0;row<3;++row)for(int column=0;column<4;++column)camera[row]+=native.view[row][column]*point[column];
    double clip[4]{};
    const auto& projection=native.rasterProjectionValid?native.rasterProjection:native.projection;
    for(int row=0;row<4;++row)for(int column=0;column<4;++column)clip[row]+=projection[row][column]*camera[column];
    return {clip[0],clip[1],clip[2]+(native.rasterProjectionValid?0:clip[3]),clip[3]};
}
bool triviallyClipped(const ClipPoint (&triangle)[3]) {
    unsigned common=0x7f;
    for(const auto& p:triangle) {
        unsigned planes=0;
        if(p.w<=0)planes|=1;
        if(p.x < -p.w)planes|=2;
        if(p.x > p.w)planes|=4;
        if(p.y < -p.w)planes|=8;
        if(p.y > p.w)planes|=16;
        if(p.z < 0)planes|=32;
        if(p.z > p.w)planes|=64;
        common &= planes;
    }
    return common!=0;
}
bool boundsVisible(const Bounds& bounds,const NativeSnapshot& native,const RenderPlacement& placement) {
    if(!bounds.valid)return false;
    unsigned common=0x7f;
    for(unsigned corner=0;corner<8;++corner){
        Vertex vertex{};vertex.x=corner&1?bounds.max.x:bounds.min.x;vertex.y=corner&2?bounds.max.y:bounds.min.y;vertex.z=corner&4?bounds.max.z:bounds.min.z;
        auto p=project(placed(vertex,placement),native);unsigned planes{};
        if(p.w<=0)planes|=1;if(p.x<-p.w)planes|=2;if(p.x>p.w)planes|=4;if(p.y<-p.w)planes|=8;if(p.y>p.w)planes|=16;if(p.z<0)planes|=32;if(p.z>p.w)planes|=64;
        common&=planes;
    }
    return common==0;
}
struct GeometryClip {
    size_t vertices{},triangles{},candidates{};
    double minY{std::numeric_limits<double>::infinity()},maxY{-std::numeric_limits<double>::infinity()};
    double minNdcY{std::numeric_limits<double>::infinity()},maxNdcY{-std::numeric_limits<double>::infinity()};
};
NativeSnapshot viewportCamera(const NativeSnapshot& native,const D3DVIEWPORT9& viewport){
    NativeSnapshot adjusted=native;
    if(native.rasterProjectionValid)return adjusted;
    // pG retains the GameCube projection. Compensate its aspect for our
    // actual draw viewport so exported cubes keep their screen proportions.
    if(viewport.Width&&viewport.Height&&native.projection[0][0]>0&&native.projection[1][1]>0&&native.projection[3][2]<0){
        float nativeAspect=native.projection[1][1]/native.projection[0][0];
        float displayAspect=static_cast<float>(viewport.Width)/viewport.Height;
        float correction=nativeAspect/displayAspect;
        if(std::isfinite(correction)&&correction>.2f&&correction<5.f)for(float& entry:adjusted.projection[0])entry*=correction;
    }
    return adjusted;
}
GeometryClip inspect(const std::vector<RenderBatch>& batches,const NativeSnapshot& native,const RenderPlacement& placement) {
    GeometryClip out;
    for(const auto& batch:batches)for(size_t first=0;first+2<triangleVertexCount(batch);first+=3) {
        ClipPoint triangle[3]{};
        for(size_t vertex=0;vertex<3;++vertex) {
            Vec3 world=placed(batch.vertices[triangleVertexIndex(batch,first+vertex)],placement);triangle[vertex]=project(world,native);
            out.minY=std::min(out.minY,double(world.y));out.maxY=std::max(out.maxY,double(world.y));
            if(triangle[vertex].w>1e-8) { double y=triangle[vertex].y/triangle[vertex].w;out.minNdcY=std::min(out.minNdcY,y);out.maxNdcY=std::max(out.maxNdcY,y); }
        }
        out.vertices+=3;++out.triangles;
        if(!triviallyClipped(triangle))++out.candidates;
    }
    return out;
}
struct StateGuard {
    IDirect3DStateBlock9* block{};
    IDirect3DDevice9* device{};
    D3DMATRIX world{},view{},projection{};
    D3DVIEWPORT9 viewport{};
    bool good{};
    explicit StateGuard(IDirect3DDevice9* d):device(d) {
        if (FAILED(d->GetTransform(D3DTS_WORLD,&world)) || FAILED(d->GetTransform(D3DTS_VIEW,&view)) || FAILED(d->GetTransform(D3DTS_PROJECTION,&projection)) || FAILED(d->GetViewport(&viewport))) return;
        good=SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&block)) && block && SUCCEEDED(block->Capture());
    }
    ~StateGuard() {
        if (block) { block->Apply(); block->Release(); }
        if (good) { device->SetTransform(D3DTS_WORLD,&world); device->SetTransform(D3DTS_VIEW,&view); device->SetTransform(D3DTS_PROJECTION,&projection); device->SetViewport(&viewport); }
    }
};
}

struct Renderer::Impl {
    struct GpuBatch { IDirect3DVertexBuffer9* vertices{};size_t count{},capacity{};bool dynamic{},morph{};UINT stride{sizeof(FixedVertex)};DWORD fvf{AvatarFVF}; IDirect3DIndexBuffer9* indices{};size_t indexCount{},indexCapacity{};bool dynamicIndices{}; };
    struct TextureEntry {
        uint32_t width{},height{},format{4};
        std::vector<uint8_t> rgba;
        bool compressed{};
        uint64_t revision{},gpuRevision{};
        IDirect3DTexture9* gpu{};
    };
    struct AtlasRegion { uint32_t x{},y{},width{},height{}; };
    struct AtlasSource {
        uint32_t id{},width{},height{};uint64_t revision{};
        bool operator==(const AtlasSource& other)const{
            return id==other.id&&width==other.width&&height==other.height&&revision==other.revision;
        }
    };
    mutable std::mutex mutex;
    std::map<uint32_t,TextureEntry> textures;
    std::vector<RenderBatch> avatar,scene,dynamic;
    std::vector<GpuBatch> avatarGpu,sceneGpu,dynamicGpu;
    struct DeformationFrame {uint64_t revision{};std::vector<std::vector<VehicleDeformationVertex>> batches;Bounds bounds;std::vector<VehicleDeformationFit> fits;};
    std::deque<DeformationFrame> deformation;
    std::vector<std::vector<uint32_t>> deformationSamples;
    std::vector<std::vector<uint32_t>> deformationWeights;
    VehicleDeformationClock deformationClock;
    std::vector<GpuBatch> deformationGpu;
    uint64_t deformationFromRevision{},deformationToRevision{};
    uint64_t deformationUploads{},deformationDraws{};
    uint32_t captureVehicleId{},presentationVehicleId{};
    double presentationSourceSeconds{-1};
    struct DeformationTransform {VehicleQuaternion partial{},inverse{};VehicleVector center{};double fraction{1};bool safe{};};
    std::vector<DeformationTransform> deformationTransforms;
    const DeformationFrame *drawDeformationFrom{},*drawDeformationTo{};
    GpuBatch transparentGpu;
    TextureEntry transparentAtlas;
    std::map<uint32_t,AtlasRegion> atlasRegions;
    std::vector<AtlasSource> atlasSignature;
    std::vector<FixedVertex> scratch;
    std::vector<FixedVehicleVertex> vehicleScratch;
    std::vector<FixedMorphVehicleVertex> morphScratch;
    struct MaterialEntry {VehicleMaterial header{};std::vector<VehicleMaterialLayer> layers;IDirect3DPixelShader9* shader{};std::vector<uint32_t> samplerIds,shaderSignature;uint64_t revision{},gpuRevision{};};
    std::map<uint32_t,MaterialEntry> materials;
    IDirect3DVertexShader9* vehicleShader{};
    IDirect3DVertexShader9* deformationShader{};
    IDirect3DVertexDeclaration9* deformationDeclaration{};
    IDirect3DCubeTexture9* environment{};
    std::string shaderError;
    VehicleQuaternion captureOrientation{},presentationOrientation{};
    double captureSeconds{};
    uint32_t captureEpoch{},presentationEpoch{};
    uint64_t avatarRevision{},sceneRevision{},avatarGpuRevision{},sceneGpuRevision{};
    uint64_t dynamicRevision{},dynamicGpuRevision{};
    Vec3 avatarOrigin{},sceneOrigin{},dynamicOrigin{};
    uint32_t avatarEpoch{},sceneEpoch{};
    uint32_t dynamicEpoch{};
    Bounds avatarBounds,sceneBounds,dynamicBounds;
    IDirect3DDevice9* device{};
    size_t textureBytes{};
    size_t textureLogicalBytes{};
    uint32_t textureDimensionLimit{2048};
    size_t textureByteLimit{64u*1024u*1024u};
    bool pendingClear{};
    bool vehicleAssets{};
    bool vehicleAtlasEnabled{true};
    void clearDeformation(){deformation.clear();deformationSamples.clear();deformationWeights.clear();deformationClock.clear();drawDeformationFrom=drawDeformationTo=nullptr;deformationTransforms.clear();deformationFromRevision=deformationToRevision=0;}
    static void releaseBuffers(std::vector<GpuBatch>& batches) {
        for(auto& batch:batches){if(batch.vertices)batch.vertices->Release();if(batch.indices)batch.indices->Release();}batches.clear();
    }
    void releaseGpu(bool clearDeformationHistory=true) {
        for (auto& pair:textures) { if (pair.second.gpu) pair.second.gpu->Release(); pair.second.gpu=nullptr; pair.second.gpuRevision=0; }
        device=nullptr;
        releaseBuffers(avatarGpu);releaseBuffers(sceneGpu);avatarGpuRevision=sceneGpuRevision=0;
        releaseBuffers(dynamicGpu);dynamicGpuRevision=0;
        releaseBuffers(deformationGpu);deformationFromRevision=deformationToRevision=0;
        if(clearDeformationHistory)clearDeformation();
        if(transparentGpu.vertices)transparentGpu.vertices->Release();if(transparentGpu.indices)transparentGpu.indices->Release();transparentGpu={};
        if(transparentAtlas.gpu)transparentAtlas.gpu->Release();transparentAtlas.gpu=nullptr;transparentAtlas.gpuRevision=0;
        if(vehicleShader)vehicleShader->Release();vehicleShader=nullptr;
        if(deformationShader)deformationShader->Release();deformationShader=nullptr;
        if(deformationDeclaration)deformationDeclaration->Release();deformationDeclaration=nullptr;
        if(environment)environment->Release();environment=nullptr;
        for(auto& item:materials){if(item.second.shader)item.second.shader->Release();item.second.shader=nullptr;item.second.gpuRevision=0;}
    }
    void retirePendingResources(){
        if(!pendingClear)return;
        // New session packets may already have arrived after clear(). Only the
        // previous GPU objects are retired; current CPU maps/poses remain valid.
        auto* associatedDevice=device;releaseGpu(false);device=associatedDevice;
        for(auto iter=textures.begin();iter!=textures.end();){if(iter->second.rgba.empty())iter=textures.erase(iter);else ++iter;}
        for(auto iter=materials.begin();iter!=materials.end();){if(iter->second.layers.empty())iter=materials.erase(iter);else ++iter;}
        pendingClear=false;
    }
    bool validTexture(uint32_t id,uint32_t width,uint32_t height,const uint8_t* pixels,size_t bytes,uint32_t format,bool compressed=false,size_t capacity=0)const{
        const size_t logicalBytes=size_t(width)*height*(format&255);
        if(!id||!width||!height||width>textureDimensionLimit||height>textureDimensionLimit||!pixels||
           (format!=4&&(!vehicleAssets||(format!=1&&format!=2&&format!=260)))||
           (compressed?(!vehicleAssets||!bytes||bytes>=logicalBytes||logicalBytes>MaxVehicleTextureMapBytes):bytes!=logicalBytes))return false;
        auto old=textures.find(id);const size_t oldBytes=old==textures.end()?0:old->second.rgba.size();
        if(textureBytes-oldBytes+bytes>textureByteLimit)return false;
        const size_t oldCapacity=old==textures.end()?0:old->second.rgba.capacity();size_t allocated{};
        for(const auto& item:textures)allocated+=item.second.rgba.capacity();
        if(allocated-oldCapacity+(capacity?capacity:bytes)>textureByteLimit)return false;
        const size_t oldLogical=oldBytes?size_t(old->second.width)*old->second.height*(old->second.format&255):0;
        if(textureLogicalBytes-oldLogical+logicalBytes>(vehicleAssets?MaxVehicleDecodedTextureBytes:textureByteLimit))return false;
        if(!oldBytes){size_t active{};for(const auto& item:textures)active+=!item.second.rgba.empty();if(active>=MaxTextures)return false;}
        return true;
    }
    void storeTexture(uint32_t id,uint32_t width,uint32_t height,std::vector<uint8_t>&& pixels,uint32_t format,bool compressed=false){
        auto& entry=textures[id];const size_t oldBytes=entry.rgba.size(),bytes=pixels.size();
        const size_t oldLogical=oldBytes?size_t(entry.width)*entry.height*(entry.format&255):0;
        if(vehicleAssets&&oldBytes&&(entry.width!=width||entry.height!=height||entry.format!=format||entry.compressed!=compressed||oldBytes!=bytes||std::memcmp(entry.rgba.data(),pixels.data(),bytes)))clearDeformation();
        if(entry.format!=format)for(auto& material:materials)for(const auto& layer:material.second.layers)
            if(std::find(std::begin(layer.maps),std::end(layer.maps),id)!=std::end(layer.maps)){material.second.gpuRevision=0;break;}
        entry.rgba=std::move(pixels);entry.width=width;entry.height=height;entry.format=format;entry.compressed=compressed;++entry.revision;
        textureBytes=textureBytes-oldBytes+bytes;
        textureLogicalBytes=textureLogicalBytes-oldLogical+size_t(width)*height*(format&255);
    }
    bool uploadTransparentBytes(IDirect3DDevice9* d,const void* data,size_t count,UINT stride,DWORD fvf) {
        auto& batch=transparentGpu;batch.count=count;batch.morph=false;
        if(!batch.count)return true;
        if(!batch.vertices||batch.capacity<batch.count||batch.stride!=stride){
            if(batch.vertices)batch.vertices->Release();batch.vertices=nullptr;batch.capacity=batch.count;
            batch.stride=stride;batch.fvf=fvf;
            batch.dynamic=SUCCEEDED(d->CreateVertexBuffer(static_cast<UINT>(batch.capacity*stride),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,fvf,D3DPOOL_DEFAULT,&batch.vertices,nullptr));
            if(!batch.dynamic&&FAILED(d->CreateVertexBuffer(static_cast<UINT>(batch.capacity*stride),D3DUSAGE_WRITEONLY,fvf,D3DPOOL_MANAGED,&batch.vertices,nullptr)))return false;
        }
        void* mapped{};if(FAILED(batch.vertices->Lock(0,static_cast<UINT>(batch.count*stride),&mapped,batch.dynamic?D3DLOCK_DISCARD:0))||!mapped)return false;
        std::memcpy(mapped,data,count*stride);return SUCCEEDED(batch.vertices->Unlock());
    }
    bool uploadTransparent(IDirect3DDevice9* d,const std::vector<FixedVertex>& vertices){return uploadTransparentBytes(d,vertices.data(),vertices.size(),sizeof(FixedVertex),AvatarFVF);}
    bool ensureIndices(IDirect3DDevice9* d,GpuBatch& gpu,const std::vector<uint32_t>& indices){
        gpu.indexCount=indices.size();
        if(indices.empty()){if(gpu.indices)gpu.indices->Release();gpu.indices=nullptr;gpu.indexCapacity=0;return true;}
        if(!gpu.indices||gpu.indexCapacity<indices.size()){
            if(gpu.indices)gpu.indices->Release();gpu.indices=nullptr;gpu.indexCapacity=indices.size();
            const UINT bytes=static_cast<UINT>(indices.size()*sizeof(uint32_t));
            gpu.dynamicIndices=SUCCEEDED(d->CreateIndexBuffer(bytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&gpu.indices,nullptr));
            if(!gpu.dynamicIndices&&FAILED(d->CreateIndexBuffer(bytes,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&gpu.indices,nullptr)))return false;
        }
        void* mapped{};if(FAILED(gpu.indices->Lock(0,static_cast<UINT>(indices.size()*sizeof(uint32_t)),&mapped,gpu.dynamicIndices?D3DLOCK_DISCARD:0))||!mapped)return false;
        std::memcpy(mapped,indices.data(),indices.size()*sizeof(uint32_t));return SUCCEEDED(gpu.indices->Unlock());
    }
    bool ensureBuffers(IDirect3DDevice9* d,const std::vector<RenderBatch>& cpu,std::vector<GpuBatch>& gpu,uint64_t revision,uint64_t& uploaded) {
        if(uploaded==revision&&gpu.size()==cpu.size())return true;
        if(gpu.size()!=cpu.size()){releaseBuffers(gpu);gpu.resize(cpu.size());}
        for(size_t i=0;i<cpu.size();++i) {
            auto& batch=gpu[i];batch.count=cpu[i].vertices.size();
            if(!batch.count||(cpu[i].flags&2))continue;
            if(!ensureIndices(d,batch,cpu[i].indices)){releaseBuffers(gpu);return false;}
            const bool surface=cpu[i].materialId!=0;const UINT stride=surface?sizeof(FixedVehicleVertex):sizeof(FixedVertex);const DWORD fvf=surface?VehicleFVF:AvatarFVF;
            if(!batch.vertices||batch.capacity<batch.count||batch.stride!=stride) {
                if(batch.vertices)batch.vertices->Release();batch.vertices=nullptr;batch.capacity=batch.count;
                batch.stride=stride;batch.fvf=fvf;
                // New poses/blocks arrive while the previous frame may still
                // use this buffer. DISCARD gives the driver a fresh allocation
                // instead of waiting for that GPU read to finish. DEFAULT
                // resources are released by onDeviceLost before native Reset.
                batch.dynamic=SUCCEEDED(d->CreateVertexBuffer(static_cast<UINT>(batch.capacity*stride),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,fvf,D3DPOOL_DEFAULT,&batch.vertices,nullptr));
                if(!batch.dynamic&&FAILED(d->CreateVertexBuffer(static_cast<UINT>(batch.capacity*stride),D3DUSAGE_WRITEONLY,fvf,D3DPOOL_MANAGED,&batch.vertices,nullptr))){releaseBuffers(gpu);return false;}
            }
            void* mapped{};if(FAILED(batch.vertices->Lock(0,static_cast<UINT>(batch.count*stride),&mapped,batch.dynamic?D3DLOCK_DISCARD:0))||!mapped){releaseBuffers(gpu);return false;}
            for(size_t n=0;n<batch.count;++n){const auto& v=cpu[i].vertices[n];
                if(surface){const auto& uv=cpu[i].secondaryUV[n];static_cast<FixedVehicleVertex*>(mapped)[n]={v.x,v.y,v.z,v.nx,v.ny,v.nz,v.color,v.u,v.v,uv[0],uv[1]};}
                else static_cast<FixedVertex*>(mapped)[n]={v.x,v.y,v.z,v.nx,v.ny,v.nz,v.color,v.u,v.v};}
            if(FAILED(batch.vertices->Unlock())){releaseBuffers(gpu);return false;}
        }
        uploaded=revision;return true;
    }
    bool ensureDeformationBuffers(IDirect3DDevice9* d,const DeformationFrame& from,const DeformationFrame& to){
        if(deformationFromRevision==from.revision&&deformationToRevision==to.revision&&deformationGpu.size()==avatar.size())return true;
        if(deformationGpu.size()!=avatar.size()){releaseBuffers(deformationGpu);deformationGpu.resize(avatar.size());}
        for(size_t i=0;i<avatar.size();++i){auto& gpu=deformationGpu[i];const auto& batch=avatar[i];gpu.count=batch.vertices.size();
            if(!gpu.count||(batch.flags&2))continue;
            if(!ensureIndices(d,gpu,batch.indices)){releaseBuffers(deformationGpu);return false;}
            const bool morph=to.fits[i].safe;const UINT stride=morph?sizeof(FixedMorphVehicleVertex):sizeof(FixedVehicleVertex);
            if(!gpu.vertices||gpu.capacity!=gpu.count||gpu.stride!=stride){if(gpu.vertices)gpu.vertices->Release();gpu.vertices=nullptr;gpu.capacity=gpu.count;gpu.stride=stride;gpu.fvf=VehicleFVF;
                gpu.dynamic=SUCCEEDED(d->CreateVertexBuffer(static_cast<UINT>(gpu.count*stride),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&gpu.vertices,nullptr));
                if(!gpu.dynamic&&FAILED(d->CreateVertexBuffer(static_cast<UINT>(gpu.count*stride),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&gpu.vertices,nullptr))){releaseBuffers(deformationGpu);return false;}}
            gpu.morph=morph;void* mapped{};if(FAILED(gpu.vertices->Lock(0,static_cast<UINT>(gpu.count*stride),&mapped,gpu.dynamic?D3DLOCK_DISCARD:0))||!mapped)return false;
            for(size_t n=0;n<gpu.count;++n){const auto& v=batch.vertices[n];const auto& uv=batch.secondaryUV[n];const auto& current=to.batches[i][n];
                FixedVehicleVertex out{current.x,current.y,current.z,current.nx,current.ny,current.nz,v.color,v.u,v.v,uv[0],uv[1]};
                if(morph){const auto& old=from.batches[i][n];static_cast<FixedMorphVehicleVertex*>(mapped)[n]={out,old.x,old.y,old.z,old.nx,old.ny,old.nz};}
                else static_cast<FixedVehicleVertex*>(mapped)[n]=out;}
            if(FAILED(gpu.vertices->Unlock()))return false;
        }
        deformationFromRevision=from.revision;deformationToRevision=to.revision;++deformationUploads;return true;
    }
    VehicleVector deformationPoint(size_t batch,size_t vertex)const {
        const auto& current=drawDeformationTo->batches[batch][vertex];const auto& transform=deformationTransforms[batch];
        if(!transform.safe)return deformationPosition(current);
        const auto& old=drawDeformationFrom->batches[batch][vertex];const auto& fit=drawDeformationTo->fits[batch];
        const auto shape=vehicleLerp(deformationSubtract(deformationPosition(old),fit.fromCenter),vehicleRotate(transform.inverse,deformationSubtract(deformationPosition(current),fit.toCenter)),transform.fraction);
        const auto p=vehicleRotate(transform.partial,shape);return {p.x+transform.center.x,p.y+transform.center.y,p.z+transform.center.z};
    }
    RenderPlacement placement(const RenderPlacement& fallback,uint32_t category)const {
        auto out=fallback;uint32_t epoch=category==3?dynamicEpoch:category==2?sceneEpoch:avatarEpoch;
        if(epoch){out.origin=category==3?dynamicOrigin:category==2?sceneOrigin:avatarOrigin;out.yawRadians=0;}
        if(category==1&&vehicleAssets&&captureEpoch&&captureEpoch==presentationEpoch&&captureEpoch==epoch){out.orientation=vehicleQuaternionDelta(presentationOrientation,captureOrientation);out.useOrientation=true;}
        return out;
    }
    bool ensureTexture(IDirect3DDevice9* d,TextureEntry& entry) {
        if (entry.gpu && entry.gpuRevision==entry.revision) return true;
        std::vector<uint8_t> decoded;
        if(entry.compressed){
            // Decode is cold upload/Reset work. A64MiB allocation can still
            // fail inside the x86 host; retain this source revision and return
            // a failed world pass rather than terminating the render thread.
            try{if(!decodedTexture(entry.rgba,size_t(entry.width)*entry.height*(entry.format&255),decoded))return false;}
            catch(const std::bad_alloc&){return false;}
        }
        const uint8_t* pixels=entry.compressed?decoded.data():entry.rgba.data();
        if (entry.gpu) { entry.gpu->Release(); entry.gpu=nullptr; }
        const uint32_t channels=entry.format&255;
        const D3DFORMAT format=channels==1?D3DFMT_L8:channels==2?D3DFMT_A8L8:D3DFMT_A8R8G8B8;
        if (FAILED(d->CreateTexture(entry.width,entry.height,1,0,format,vehicleAssets?D3DPOOL_DEFAULT:D3DPOOL_MANAGED,&entry.gpu,nullptr))) return false;
        IDirect3DTexture9* staging{};
        if(vehicleAssets&&FAILED(d->CreateTexture(entry.width,entry.height,1,0,format,D3DPOOL_SYSTEMMEM,&staging,nullptr))){entry.gpu->Release();entry.gpu=nullptr;return false;}
        IDirect3DTexture9* upload=staging?staging:entry.gpu;
        D3DLOCKED_RECT locked{};
        if (FAILED(upload->LockRect(0,&locked,nullptr,0))) { if(staging)staging->Release();entry.gpu->Release(); entry.gpu=nullptr; return false; }
        for (uint32_t y=0;y<entry.height;++y) {
            uint8_t* dst=static_cast<uint8_t*>(locked.pBits)+static_cast<size_t>(y)*locked.Pitch;
            const uint8_t* src=pixels+static_cast<size_t>(y)*entry.width*channels;
            if(channels!=4)std::memcpy(dst,src,size_t(entry.width)*channels);
            else for (uint32_t x=0;x<entry.width;++x) { dst[4*x]=src[4*x+2]; dst[4*x+1]=src[4*x+1]; dst[4*x+2]=src[4*x]; dst[4*x+3]=src[4*x+3]; }
        }
        HRESULT result=upload->UnlockRect(0);
        if(staging){if(SUCCEEDED(result))result=d->UpdateTexture(staging,entry.gpu);staging->Release();}
        if(FAILED(result)){entry.gpu->Release();entry.gpu=nullptr;return false;}
        entry.gpuRevision=entry.revision;
        return true;
    }
    bool compileShader(const std::string& source,const char* target,ID3DBlob** code){
        // Use the system compiler rather than any game-directory DLL override.
        static HMODULE compiler=LoadLibraryExW(L"d3dcompiler_47.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        static auto compile=compiler?reinterpret_cast<decltype(&D3DCompile)>(GetProcAddress(compiler,"D3DCompile")):nullptr;
        if(!compile){shaderError="System D3DCompiler_47 unavailable";return false;}
        ID3DBlob* errors{};HRESULT result=compile(source.data(),source.size(),"RE4Craft vehicle material",nullptr,nullptr,"main",target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code,&errors);
        if(FAILED(result))shaderError=errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"Vehicle shader compilation failed";
        if(errors)errors->Release();return SUCCEEDED(result)&&*code;
    }
    bool ensureVehicleShader(IDirect3DDevice9* d){
        if(vehicleShader&&environment)return true;
        D3DCAPS9 caps{};if(FAILED(d->GetDeviceCaps(&caps))||caps.VertexShaderVersion<D3DVS_VERSION(3,0)||caps.PixelShaderVersion<D3DPS_VERSION(3,0)){shaderError="Vehicle materials require shader model 3";return false;}
        if(!vehicleShader){
            const std::string source=R"HLSL(
row_major float4x4 worldViewProjection : register(c0);
row_major float4x4 world : register(c4);
struct Input {float4 p:POSITION0;float3 n:NORMAL0;float4 color:COLOR0;float2 uv:TEXCOORD0;float2 uv2:TEXCOORD1;};
struct Output {float4 p:POSITION0;float3 worldP:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float2 uv2:TEXCOORD3;float4 color:COLOR0;};
Output main(Input v){Output o;o.p=mul(v.p,worldViewProjection);o.worldP=mul(v.p,world).xyz;o.n=normalize(mul(float4(v.n,0),world).xyz);o.uv=v.uv;o.uv2=v.uv2;o.color=v.color;return o;}
)HLSL";
            ID3DBlob* code{};if(!compileShader(source,"vs_3_0",&code))return false;
            HRESULT result=d->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),&vehicleShader);code->Release();if(FAILED(result))return false;
        }
        if(!environment){
            // A small static daytime sky/ground approximation. It performs no
            // RE4 framebuffer readback and does not alter source materials.
            constexpr uint32_t side=32;
            if(FAILED(d->CreateCubeTexture(side,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&environment,nullptr)))return false;
            for(unsigned face=0;face<6;face++){
                D3DLOCKED_RECT locked{};if(FAILED(environment->LockRect(static_cast<D3DCUBEMAP_FACES>(face),0,&locked,nullptr,0))){environment->Release();environment=nullptr;return false;}
                for(unsigned y=0;y<side;y++)for(unsigned x=0;x<side;x++){
                    const float u=2*(float(x)+.5f)/side-1,v=2*(float(y)+.5f)/side-1;
                    VehicleVector direction{};switch(face){case 0:direction={1,-v,-u};break;case 1:direction={-1,-v,u};break;case 2:direction={u,1,v};break;case 3:direction={u,-1,-v};break;case 4:direction={u,-v,1};break;default:direction={-u,-v,-1};}
                    const float vertical=float(direction.y/std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z));
                    const auto sky=vehicleDaylightEnvironment(vertical);
                    const unsigned r=unsigned(std::lround(255*sky[0])),g=unsigned(std::lround(255*sky[1])),b=unsigned(std::lround(255*sky[2]));
                    reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(locked.pBits)+y*locked.Pitch)[x]=0xff000000|(r<<16)|(g<<8)|b;
                }environment->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face),0);
            }
        }return true;
    }
    bool ensureDeformationShader(IDirect3DDevice9* d){
        if(deformationShader&&deformationDeclaration)return true;
        if(!ensureVehicleShader(d))return false;
        if(!deformationDeclaration){const D3DVERTEXELEMENT9 elements[]{
            {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
            {0,28,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            {0,36,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
            {0,44,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,1},
            {0,56,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,1},D3DDECL_END()};
            if(FAILED(d->CreateVertexDeclaration(elements,&deformationDeclaration)))return false;}
        if(!deformationShader){const std::string source=R"HLSL(
row_major float4x4 worldViewProjection:register(c0);row_major float4x4 world:register(c4);
float4 factor:register(c8);float4 fromCenter:register(c9);float4 toCenter:register(c10);
float4 inverseRotation[3]:register(c11);float4 partialRotation[3]:register(c14);float4 center:register(c17);
struct Input{float4 p:POSITION0;float3 n:NORMAL0;float4 color:COLOR0;float2 uv:TEXCOORD0;float2 uv2:TEXCOORD1;float3 oldP:POSITION1;float3 oldN:NORMAL1;};
struct Output{float4 p:POSITION0;float3 worldP:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float2 uv2:TEXCOORD3;float4 color:COLOR0;};
float3 inverseRotate(float3 v){return float3(dot(v,inverseRotation[0].xyz),dot(v,inverseRotation[1].xyz),dot(v,inverseRotation[2].xyz));}
float3 partialRotate(float3 v){return float3(dot(v,partialRotation[0].xyz),dot(v,partialRotation[1].xyz),dot(v,partialRotation[2].xyz));}
Output main(Input v){Output o;float3 shape=lerp(v.oldP-fromCenter.xyz,inverseRotate(v.p.xyz-toCenter.xyz),factor.x);
float4 p=float4(partialRotate(shape)+center.xyz,1);float3 normal=partialRotate(lerp(v.oldN,inverseRotate(v.n),factor.x));
if(dot(normal,normal)<1e-16)normal=v.n;
o.p=mul(p,worldViewProjection);o.worldP=mul(p,world).xyz;o.n=normalize(mul(float4(normal,0),world).xyz);o.uv=v.uv;o.uv2=v.uv2;o.color=v.color;return o;}
)HLSL";
            ID3DBlob* code{};if(!compileShader(source,"vs_3_0",&code))return false;
            HRESULT result=d->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()),&deformationShader);code->Release();if(FAILED(result))return false;
        }return true;
    }
    bool ensureMaterial(IDirect3DDevice9* d,MaterialEntry& material){
        if(material.shader&&material.gpuRevision==material.revision)return true;
        if(!ensureVehicleShader(d))return false;
        std::vector<uint32_t> signature{material.header.flags,uint32_t(material.layers.size())};
        for(const auto& layer:material.layers){signature.push_back(layer.flags);signature.push_back(layer.mapUV1Mask);
            for(uint32_t id:layer.maps){signature.push_back(id);auto found=textures.find(id);signature.push_back(found==textures.end()?0:found->second.format);}}
        // Paint changes, factor edits and a new host epoch only change shader
        // constants. Reuse the exact existing permutation when map semantics
        // and flags are unchanged, avoiding compile stalls on mode switches.
        if(material.shader&&signature==material.shaderSignature){material.gpuRevision=material.revision;return true;}
        if(material.shader)material.shader->Release();material.shader=nullptr;material.samplerIds.clear();
        std::map<uint32_t,unsigned> samplers;
        for(const auto& layer:material.layers)for(uint32_t id:layer.maps)if(id&&samplers.find(id)==samplers.end()){unsigned slot=unsigned(material.samplerIds.size());samplers[id]=slot;material.samplerIds.push_back(id);}
        if(material.samplerIds.size()>15){shaderError="Vehicle material exceeds 15 source map samplers";return false;}
        for(uint32_t id:material.samplerIds){auto found=textures.find(id);if(found==textures.end()||found->second.rgba.empty()||!ensureTexture(d,found->second))return false;}
        std::ostringstream source;
        source<<R"HLSL(
float4 eye:register(c0);float4 paints[3]:register(c1);float4 paintData[3]:register(c4);float4 layers[20]:register(c7);float4 settings:register(c27);
struct Input {float3 p:TEXCOORD0;float3 n:TEXCOORD1;float2 uv:TEXCOORD2;float2 uv2:TEXCOORD3;float4 color:COLOR0;};
float3 linearColor(float3 c){return lerp(c/12.92,pow(max((c+.055)/1.055,0),2.4),step(.04045,c));}
float3 gammaColor(float3 c){c=max(c,0);return lerp(c*12.92,1.055*pow(c,1./2.4)-.055,step(.0031308,c));}
float3 decodeNormal(float2 xy,float strength){xy=xy*2-1;return normalize(float3(xy*strength,sqrt(saturate(1-dot(xy,xy)))));}
float3 normalFromMap(float3 N,float3 p,float2 uv,float3 mapped){
    float3 dp1=ddx(p),dp2=ddy(p);float2 duv1=ddx(uv),duv2=ddy(uv);
    float3 a=cross(dp2,N),b=cross(N,dp1);float3 T=a*duv1.x+b*duv2.x,B=a*duv1.y+b*duv2.y;
    float len=max(dot(T,T),dot(B,B));if(len<1e-16)return N;float inv=rsqrt(len);
    return normalize(T*inv*mapped.x+B*inv*mapped.y+N*mapped.z);
}
float3 fresnel(float cosine,float3 f0){return f0+(1-f0)*pow(1-saturate(cosine),5);}
float ggx(float NH,float rough){float a=rough*rough,a2=a*a,d=NH*NH*(a2-1)+1;return a2/max(3.14159265*d*d,1e-5);}
float geometry(float NV,float rough){float k=(rough+1)*(rough+1)/8;return NV/max(NV*(1-k)+k,1e-5);}
)HLSL";
        source<<VehicleDisplayToneMapHlsl;
        source<<VehicleDaylightHlsl;
        for(unsigned slot=0;slot<material.samplerIds.size();slot++)source<<"sampler2D map"<<slot<<":register(s"<<slot<<");\n";
        source<<"samplerCUBE environment:register(s"<<material.samplerIds.size()<<");\n";
        auto uv=[&](const VehicleMaterialLayer& layer,unsigned map){return std::string((layer.mapUV1Mask&(1u<<map))?"v.uv2":"v.uv");};
        auto tex=[&](const VehicleMaterialLayer& layer,unsigned map){return "tex2D(map"+std::to_string(samplers.at(layer.maps[map]))+","+uv(layer,map)+")";};
        source<<"float4 main(Input v,float face:VFACE):COLOR0 {float3 N=normalize(v.n);";
        if(material.header.flags&1)source<<"N*=face<0?-1:1;";
        source<<"float3 base=1,emissive=0;float opacity=1,metal=0,rough=1,ao=1,coat=0,coatRough=1;\n";
        for(unsigned i=0;i<material.layers.size();i++){
            const auto& layer=material.layers[i];const unsigned c=i*5;
            source<<"{float4 color=layers["<<c<<"];float3 emission=layers["<<c+1<<"].rgb*layers["<<c+1<<"].a;float alpha=layers["<<c+2<<"].x;float m=layers["<<c+2<<"].y,r=layers["<<c+2<<"].z,a=layers["<<c+2<<"].w,cc=layers["<<c+3<<"].x,cr=layers["<<c+3<<"].y;float3 n=N;\n";
            if(layer.maps[0])source<<"float4 baseMap="<<tex(layer,0)<<";color*=baseMap;\n";
            if(!layer.maps[1]&&(layer.flags&1))source<<"color.rgb*=paints[0].rgb;\n";
            if(!layer.maps[1]&&(layer.flags&2))source<<"alpha*=paints[0].a;\n";
            if(layer.maps[1]){
                source<<"float4 mask="<<tex(layer,1)<<";float sum=dot(mask.rgb,1);float diffA=max(abs(ddx(mask.a)),abs(ddy(mask.a)));mask.a=1-pow(1-saturate(mask.a),diffA>.5?4:1);float3 weights=mask.rgb/(sum+1e-10)*mask.a;float white=1-mask.a;\n";
                if(layer.flags&4)source<<"color.rgb*=paints[0].rgb*weights.r+paints[1].rgb*weights.g+paints[2].rgb*weights.b+white;\n";
                if(layer.flags&8)source<<"r*=dot(float3(paintData[0].x,paintData[1].x,paintData[2].x),weights)+white;\n";
                if(layer.flags&16)source<<"m*=dot(float3(paintData[0].y,paintData[1].y,paintData[2].y),weights)+white;\n";
                if(layer.flags&32)source<<"cc*=dot(float3(paintData[0].z,paintData[1].z,paintData[2].z),weights)+white;\n";
                if(layer.flags&64)source<<"cr*=dot(float3(paintData[0].w,paintData[1].w,paintData[2].w),weights)+white;\n";
            }
            if(layer.maps[2])source<<"alpha*="<<tex(layer,2)<<".r;\n";
            for(auto role:{std::pair<unsigned,const char*>{3,"m"},{4,"r"},{6,"a"},{7,"cc"},{8,"cr"}})if(layer.maps[role.first])source<<role.second<<"*="<<tex(layer,role.first)<<".r;\n";
            if(layer.maps[5]||layer.maps[10]){
                source<<"float3 mapped=float3(0,0,1);\n";
                if(layer.maps[5]){const std::string channels=(textures.at(layer.maps[5]).format&255)==2?".ra":".rg";
                    source<<"mapped=decodeNormal("<<tex(layer,5)<<channels<<",layers["<<c+3<<"].z);\n";}
                if(layer.maps[10]){const std::string channels=(textures.at(layer.maps[10]).format&255)==2?".ra":".rg";
                    source<<"mapped.xy+=decodeNormal(tex2D(map"<<samplers.at(layer.maps[10])<<","<<uv(layer,10)<<"*layers["<<c+4<<"].xy)"<<channels<<",1).xy*layers["<<c+3<<"].w;mapped=normalize(mapped);\n";}
                source<<"n=normalFromMap(N,v.p,"<<uv(layer,layer.maps[5]?5:10)<<",mapped);\n";
            }
            if(layer.maps[9])source<<"emission*="<<tex(layer,9)<<".rgb;\n";
            source<<"float blend="<<(i==0?"1":"alpha")<<";base=lerp(base,color.rgb,blend);opacity=lerp(opacity,alpha,blend);metal=lerp(metal,m,blend);rough=lerp(rough,r,blend);ao=lerp(ao,a,blend);coat=lerp(coat,cc,blend);coatRough=lerp(coatRough,cr,blend);emissive=lerp(emissive,emission,blend);N=normalize(lerp(N,n,blend));}\n";
        }
        source<<"opacity=saturate(opacity)*v.color.a;base*=linearColor(v.color.rgb);";
        if(material.header.flags&4)source<<"clip(opacity-settings.x);";
        if(((material.header.flags>>8)&7)==5)source<<"base*=opacity;";
        source<<R"HLSL(
float3 V=normalize(eye.xyz-v.p),L=vehicleDaylightSunDirection(),H=normalize(V+L);float NV=max(dot(N,V),.001),NL=max(dot(N,L),0),NH=max(dot(N,H),0),VH=max(dot(V,H),0);
metal=saturate(metal);rough=clamp(rough,.045,1);coat=saturate(coat);coatRough=clamp(coatRough,.045,1);
float3 f0=max(.001,saturate(lerp(.04,base,metal))),F=fresnel(VH,f0);
float3 diffuse=saturate((1-metal)*base);float3 directDiffuse=diffuse*(1-F)/3.14159265*NL;
float3 directSpecular=F*ggx(NH,rough)*geometry(NV,rough)*geometry(NL,rough)/max(4*NV*max(NL,.001),.001)*NL;
)HLSL";
        source<<"float3 reflected=reflect(-V,N),env="<<((material.header.flags&16)?"float3(0,0,0)":"texCUBE(environment,reflected).rgb")<<",ambient=texCUBE(environment,N).rgb;\n";
        source<<R"HLSL(
float3 reflection=env*fresnel(NV,f0)*(1-.65*rough)*vehicleDaylightReflectionScale;
float coatF=.04+.96*pow(1-NV,5);float coatDirect=(.04+.96*pow(1-VH,5))*ggx(NH,coatRough)*geometry(NV,coatRough)*geometry(NL,coatRough)/max(4*NV*max(NL,.001),.001)*NL;
float3 result=((directDiffuse+directSpecular*vehicleDaylightDirectSpecularScale)*vehicleDaylightDirectScale+diffuse*ambient*vehicleDaylightAmbientScale+reflection)*saturate(ao)*(1-coat*coatF)+coat*(env*coatF*(1-.65*coatRough)*vehicleDaylightReflectionScale+coatDirect*vehicleDaylightDirectScale*vehicleDaylightDirectSpecularScale)+emissive;
// The host target is SDR A8R8G8B8. Preserve the source HDR lighting range until
// display tone mapping, rather than letting attachment saturation erase paint.
return float4(gammaColor(vehicleDisplayLinear(result)),opacity);}
)HLSL";
        ID3DBlob* code{};if(!compileShader(source.str(),"ps_3_0",&code))return false;
        HRESULT result=d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&material.shader);code->Release();if(FAILED(result))return false;
        material.shaderSignature=std::move(signature);material.gpuRevision=material.revision;shaderError.clear();return true;
    }
    bool ensureTransparentAtlas(IDirect3DDevice9* d,const std::vector<uint32_t>& ids){
        std::vector<AtlasSource> signature;signature.reserve(ids.size());
        for(uint32_t id:ids){
            if(!id){signature.push_back({0,1,1,0});continue;}
            auto found=textures.find(id);
            if(found==textures.end()||found->second.rgba.empty()||found->second.format!=4)return false;
            const auto& source=found->second;
            if(source.compressed)return false; // exact individual-map fallback
            signature.push_back({id,source.width,source.height,source.revision});
        }
        if(signature!=atlasSignature){
            atlasSignature=signature;atlasRegions.clear();
            if(transparentAtlas.gpu)transparentAtlas.gpu->Release();transparentAtlas.gpu=nullptr;transparentAtlas.gpuRevision=0;
            transparentAtlas.rgba.clear();transparentAtlas.width=transparentAtlas.height=0;++transparentAtlas.revision;
            // Full source rectangles include a one-texel duplicated edge on
            // every side. No source texel is resized, filtered, or discarded.
            for(const auto& source:signature)if(source.width>4094||source.height>4094)return false;
            auto packing=signature;
            std::stable_sort(packing.begin(),packing.end(),[](const auto& a,const auto& b){
                return a.height==b.height?a.width>b.width:a.height>b.height;
            });
            uint32_t side=1;
            for(const auto& source:packing)while(side<source.width+2||side<source.height+2)side*=2;
            for(;side<=4096;side*=2){
                uint32_t x=0,y=0,rowHeight=0;std::map<uint32_t,AtlasRegion> regions;bool fits=true;
                for(const auto& source:packing){
                    auto alignment=[](uint32_t extent){uint32_t value=1;while(value<extent)value*=2;return std::min(512u,std::max(1u,value/2));};
                    const uint32_t alignX=alignment(source.width),alignY=alignment(source.height);
                    auto aligned=[](uint32_t value,uint32_t align){return (value+align-1)/align*align;};
                    uint32_t innerX=aligned(x+1,alignX),innerY=aligned(y+1,alignY);
                    if(innerX+source.width+1>side){x=0;y+=rowHeight;rowHeight=0;innerX=aligned(1,alignX);innerY=aligned(y+1,alignY);}
                    if(innerX+source.width+1>side||innerY+source.height+1>side){fits=false;break;}
                    // Binary-aligned interior origins retain more source UV
                    // precision than placing full maps at odd texel offsets.
                    regions[source.id]={innerX,innerY,source.width,source.height};
                    x=innerX+source.width+1;rowHeight=std::max(rowHeight,innerY+source.height+1-y);
                }
                if(!fits)continue;
                transparentAtlas.width=transparentAtlas.height=side;
                transparentAtlas.rgba.resize(size_t(side)*side*4);
                for(const auto& source:packing){
                    const auto& region=regions.at(source.id);
                    if(!source.id){
                        for(uint32_t row=0;row<3;row++)std::memset(transparentAtlas.rgba.data()+(size_t(region.y-1+row)*side+region.x-1)*4,255,12);
                        continue;
                    }
                    const auto& pixels=textures.at(source.id).rgba;
                    for(uint32_t row=0;row<source.height+2;row++){
                        const uint32_t sourceRow=row?std::min(row-1,source.height-1):0;
                        const uint8_t* in=pixels.data()+size_t(sourceRow)*source.width*4;
                        uint8_t* out=transparentAtlas.rgba.data()+(size_t(region.y-1+row)*side+region.x-1)*4;
                        std::memcpy(out,in,4);std::memcpy(out+4,in,size_t(source.width)*4);
                        std::memcpy(out+size_t(source.width+1)*4,in+size_t(source.width-1)*4,4);
                    }
                }
                atlasRegions=std::move(regions);break;
            }
        }
        return !transparentAtlas.rgba.empty()&&ensureTexture(d,transparentAtlas);
    }
};

Renderer::Renderer(bool vehicleAssets,bool vehicleAtlas):impl_(std::make_unique<Impl>()) {
    impl_->vehicleAssets=vehicleAssets;
    impl_->vehicleAtlasEnabled=vehicleAtlas;
    if(vehicleAssets){impl_->textureDimensionLimit=4096;impl_->textureByteLimit=384u*1024u*1024u;}
}
Renderer::~Renderer() { impl_->releaseGpu(); }

bool Renderer::uploadTexture(uint32_t id,uint32_t width,uint32_t height,const uint8_t* rgba,size_t bytes,uint32_t format) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!impl_->validTexture(id,width,height,rgba,bytes,format))return false;
    // Compatibility callers retain their pixels. The packet ingestion path uses
    // the vector overload below and avoids this allocation entirely.
    std::vector<uint8_t> pixels(rgba,rgba+bytes);impl_->storeTexture(id,width,height,std::move(pixels),format);
    return true;
}
bool Renderer::uploadTexture(uint32_t id,uint32_t width,uint32_t height,std::vector<uint8_t>&& pixels,uint32_t format){
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!impl_->validTexture(id,width,height,pixels.data(),pixels.size(),format,false,pixels.capacity()))return false;
    impl_->storeTexture(id,width,height,std::move(pixels),format);
    return true;
}
bool Renderer::uploadVehicleTexture(uint32_t id,uint32_t width,uint32_t height,std::vector<uint8_t>&& storage,uint32_t format,bool compressed){
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!impl_->vehicleAssets||!impl_->validTexture(id,width,height,storage.data(),storage.size(),format,compressed,storage.capacity()))return false;
    impl_->storeTexture(id,width,height,std::move(storage),format,compressed);return true;
}

bool Renderer::setMesh(uint32_t category,std::vector<RenderBatch> batches) {
    if (category<1 || category>3) return false;
    Bounds bounds;
    if(!validateBatches(batches,impl_->vehicleAssets,bounds))return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    (category==1?impl_->avatar:category==2?impl_->scene:impl_->dynamic)=std::move(batches);
    ++(category==1?impl_->avatarRevision:category==2?impl_->sceneRevision:impl_->dynamicRevision);
    (category==1?impl_->avatarBounds:category==2?impl_->sceneBounds:impl_->dynamicBounds)=bounds;
    if(category==1&&impl_->vehicleAssets){impl_->captureEpoch=impl_->presentationEpoch=0;impl_->clearDeformation();}
    return true;
}
bool Renderer::setVehicleFrame(std::vector<RenderBatch> batches,VehicleQuaternion captured,double seconds,uint32_t epoch,uint32_t vehicleId,double arrivalMs){
    if(!impl_->vehicleAssets||!epoch||!std::isfinite(seconds)||seconds<0||!validVehicleQuaternion(captured))return false;
    Bounds bounds;if(!validateBatches(batches,true,bounds))return false;
    if(arrivalMs<0)arrivalMs=vehiclePresentationNowMs();if(!std::isfinite(arrivalMs))return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!vehicleId||impl_->captureEpoch!=epoch||impl_->captureVehicleId!=vehicleId||!compatibleVehicleDeformation(impl_->avatar,batches))impl_->clearDeformation();
    const bool identity=vehicleId&&!batches.empty()&&std::all_of(batches.begin(),batches.end(),[](const auto& b){return b.materialId&&(!b.sourceIndices.empty()||!b.indices.empty());});
    if(identity){const size_t previousCount=impl_->deformationClock.count();const bool continuous=impl_->deformationClock.push(seconds,arrivalMs,epoch,vehicleId);
        if(!continuous&&previousCount)impl_->deformation.clear();
        if(impl_->deformationSamples.size()!=batches.size()){impl_->deformationSamples.resize(batches.size());impl_->deformationWeights.resize(batches.size());for(size_t i=0;i<batches.size();++i){
            const auto& topology=batches[i].indices.empty()?batches[i].sourceIndices:batches[i].indices;
            auto representatives=vehicleDeformationRepresentatives(topology);
            impl_->deformationWeights[i]=vehicleDeformationWeights(topology,representatives);
            if(!batches[i].indices.empty())for(auto& index:representatives)index=topology[index];
            impl_->deformationSamples[i]=std::move(representatives);}}
        Impl::DeformationFrame frame;frame.revision=impl_->avatarRevision+1;frame.batches.resize(batches.size());
        captured=normalizedVehicleQuaternion(captured);const VehicleQuaternion inverse{-captured.x,-captured.y,-captured.z,captured.w};
        for(size_t i=0;i<batches.size();++i){auto& positions=frame.batches[i];positions.reserve(batches[i].vertices.size());
            for(const auto& v:batches[i].vertices){const auto position=vehicleRotate(inverse,{v.x,v.y,v.z}),normal=vehicleRotate(inverse,{v.nx,v.ny,v.nz});
                positions.push_back({float(position.x),float(position.y),float(position.z),float(normal.x),float(normal.y),float(normal.z)});
                if(!frame.bounds.valid){frame.bounds.min=frame.bounds.max={float(position.x),float(position.y),float(position.z)};frame.bounds.valid=true;}
                else{frame.bounds.min.x=std::min(frame.bounds.min.x,float(position.x));frame.bounds.min.y=std::min(frame.bounds.min.y,float(position.y));frame.bounds.min.z=std::min(frame.bounds.min.z,float(position.z));
                    frame.bounds.max.x=std::max(frame.bounds.max.x,float(position.x));frame.bounds.max.y=std::max(frame.bounds.max.y,float(position.y));frame.bounds.max.z=std::max(frame.bounds.max.z,float(position.z));}}
        }
        if(!impl_->deformation.empty()){frame.fits.reserve(batches.size());for(size_t i=0;i<batches.size();++i)frame.fits.push_back(fitVehicleDeformation(
            VehicleDeformationUniqueView{impl_->deformation.back().batches[i],impl_->deformationSamples[i],&impl_->deformationWeights[i]},VehicleDeformationUniqueView{frame.batches[i],impl_->deformationSamples[i],&impl_->deformationWeights[i]}));}
        if(impl_->deformation.size()==3)impl_->deformation.pop_front();impl_->deformation.push_back(std::move(frame));
    }
    impl_->avatar=std::move(batches);impl_->avatarBounds=bounds;++impl_->avatarRevision;
    impl_->captureOrientation=normalizedVehicleQuaternion(captured);impl_->captureSeconds=seconds;impl_->captureEpoch=epoch;
    impl_->captureVehicleId=vehicleId;
    return true;
}
void Renderer::setVehiclePose(Vec3 origin,VehicleQuaternion presented,uint32_t epoch,uint32_t vehicleId,double sourceSeconds){
    if(!impl_->vehicleAssets||!epoch||!validVehicleQuaternion(presented)||!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))return;
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->avatarOrigin=origin;impl_->avatarEpoch=epoch;
    impl_->presentationOrientation=normalizedVehicleQuaternion(presented);impl_->presentationEpoch=epoch;
    if(impl_->presentationVehicleId&&impl_->presentationVehicleId!=vehicleId)impl_->clearDeformation();
    impl_->presentationVehicleId=vehicleId;impl_->presentationSourceSeconds=sourceSeconds;
}
void Renderer::resetVehicleDeformation(){
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->clearDeformation();
}
bool Renderer::setVehicleMaterial(const VehicleMaterial& h,const std::vector<VehicleMaterialLayer>& layers){
    if(!impl_->vehicleAssets||!h.epoch||!h.id||h.id>256||h.layerCount!=layers.size()||layers.empty()||layers.size()>4||h.reserved||
       (h.flags&~(31u|0x700u))||((h.flags>>8)&7)>5||!std::isfinite(h.alphaRef)||h.alphaRef<0||h.alphaRef>1)return false;
    for(const auto& paint:h.paintColor)for(float f:paint)if(!std::isfinite(f)||f<0||f>64)return false;
    for(const auto& paint:h.paintData)for(float f:paint)if(!std::isfinite(f)||f<0||f>1)return false;
    for(const auto& layer:layers){if(layer.reserved||(layer.flags&~127u)||(layer.mapUV1Mask&~2047u))return false;
        for(uint32_t id:layer.maps)if(id>65535)return false;
        for(float f:layer.baseColor)if(!std::isfinite(f)||f<0||f>64)return false;
        for(float f:layer.emissive)if(!std::isfinite(f)||f<0||f>64)return false;
        if(!std::isfinite(layer.emissiveNits)||layer.emissiveNits<0||layer.emissiveNits>100000)return false;
        for(float f:{layer.opacity,layer.metallic,layer.roughness,layer.ao,layer.clearcoat,layer.clearcoatRoughness})if(!std::isfinite(f)||f<0||f>1)return false;
        for(float f:{layer.normalStrength,layer.detailNormalStrength})if(!std::isfinite(f)||f<0||f>16)return false;
        for(float f:{layer.detailScaleX,layer.detailScaleY})if(!std::isfinite(f)||std::abs(f)>1024)return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(impl_->materials.find(h.id)==impl_->materials.end()&&impl_->materials.size()>=MaxTextures)return false;
    auto& entry=impl_->materials[h.id];
    if(entry.revision&&(std::memcmp(&entry.header,&h,sizeof(h))||entry.layers.size()!=layers.size()||
        std::memcmp(entry.layers.data(),layers.data(),layers.size()*sizeof(VehicleMaterialLayer))))impl_->clearDeformation();
    entry.header=h;entry.layers=layers;++entry.revision;return true;
}
void Renderer::setAvatarOrigin(Vec3 origin,uint32_t epoch) {
    if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))return;
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->avatarOrigin=origin;impl_->avatarEpoch=epoch;
}
void Renderer::setSceneOrigin(Vec3 origin,uint32_t epoch) {
    if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))return;
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->sceneOrigin=origin;impl_->sceneEpoch=epoch;
}
void Renderer::setDynamicOrigin(Vec3 origin,uint32_t epoch) {
    if(!std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))return;
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->dynamicOrigin=origin;impl_->dynamicEpoch=epoch;
}

void Renderer::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    releaseStorage(impl_->avatar);releaseStorage(impl_->scene);releaseStorage(impl_->dynamic);
    impl_->avatarBounds={};impl_->sceneBounds={};impl_->dynamicBounds={};
    impl_->avatarEpoch=impl_->sceneEpoch=0;++impl_->avatarRevision;++impl_->sceneRevision;
    impl_->dynamicEpoch=0;++impl_->dynamicRevision;
    impl_->captureEpoch=impl_->presentationEpoch=0;
    impl_->captureVehicleId=impl_->presentationVehicleId=0;impl_->presentationSourceSeconds=-1;impl_->clearDeformation();
    releaseStorage(impl_->deformationSamples);releaseStorage(impl_->deformationWeights);releaseStorage(impl_->deformationTransforms);
    for(auto& item:impl_->materials){releaseStorage(item.second.layers);releaseStorage(item.second.samplerIds);releaseStorage(item.second.shaderSignature);++item.second.revision;}
    impl_->pendingClear=true;
    releaseStorage(impl_->transparentAtlas.rgba);impl_->transparentAtlas.width=impl_->transparentAtlas.height=0;
    ++impl_->transparentAtlas.revision;impl_->atlasRegions.clear();impl_->atlasSignature.clear();
    releaseStorage(impl_->atlasSignature);releaseStorage(impl_->scratch);releaseStorage(impl_->vehicleScratch);releaseStorage(impl_->morphScratch);
    // Empty vector capacity is significant in RE4's 32-bit address space.
    // GPU resources are released separately on the render/reset thread.
    for(auto& item:impl_->textures){releaseStorage(item.second.rgba);item.second.width=item.second.height=0;++item.second.revision;}
    impl_->textureBytes=impl_->textureLogicalBytes=0;
}
void Renderer::retirePendingResources(){
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->retirePendingResources();
}
RendererResourceUsage Renderer::resourceUsage()const{
    std::lock_guard<std::mutex> lock(impl_->mutex);RendererResourceUsage usage;
    usage.cpuTextureBytes=impl_->textureBytes;usage.pendingClear=impl_->pendingClear;
    usage.decodedTextureBytes=impl_->textureLogicalBytes;
    for(const auto& item:impl_->textures){usage.cpuTextureCapacityBytes+=item.second.rgba.capacity();usage.gpuTextureCount+=item.second.gpu!=nullptr;}
    usage.cpuTextureCapacityBytes+=impl_->transparentAtlas.rgba.capacity();usage.gpuTextureCount+=impl_->transparentAtlas.gpu!=nullptr;
    for(const auto& item:impl_->materials)usage.gpuMaterialCount+=item.second.shader!=nullptr;
    return usage;
}
bool Renderer::hasAvatar() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    bool vertices=false;
    for (const auto& b:impl_->avatar) {
        vertices |= !b.vertices.empty();
        if (b.textureId) { auto texture=impl_->textures.find(b.textureId); if (texture==impl_->textures.end() || texture->second.rgba.empty()) return false; }
        if(b.materialId){auto found=impl_->materials.find(b.materialId);if(found==impl_->materials.end()||found->second.layers.empty()||found->second.header.epoch!=impl_->captureEpoch)return false;
            for(const auto& layer:found->second.layers)for(uint32_t id:layer.maps)if(id){auto texture=impl_->textures.find(id);if(texture==impl_->textures.end()||texture->second.rgba.empty())return false;}}
    }
    return vertices;
}
void Renderer::onDeviceLost() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->releaseGpu();
}

std::string Renderer::describeFrame(IDirect3DDevice9* d,const NativeSnapshot& native,const RenderPlacement& p) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    IDirect3DSurface9 *target{},*back{},*depth{};D3DSURFACE_DESC rt{},bb{},ds{};D3DVIEWPORT9 vp{};
    DWORD gameZ{},gameZFunc{};
    D3DDEVICE_CREATION_PARAMETERS creation{};
    if(d) {
        d->GetRenderTarget(0,&target);d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);d->GetDepthStencilSurface(&depth);
        if(target)target->GetDesc(&rt);if(back)back->GetDesc(&bb);if(depth)depth->GetDesc(&ds);
        d->GetViewport(&vp);d->GetRenderState(D3DRS_ZENABLE,&gameZ);d->GetRenderState(D3DRS_ZFUNC,&gameZFunc);
        d->GetCreationParameters(&creation);
    }
    auto adjusted=viewportCamera(native,vp);
    auto avatarPlacement=impl_->placement(p,1);
    auto geometry=inspect(impl_->avatar,adjusted,avatarPlacement);
    auto feet=project(avatarPlacement.origin,adjusted);
    auto head=project({avatarPlacement.origin.x,avatarPlacement.origin.y+1.8f*p.worldUnitsPerBlock*p.ySign,avatarPlacement.origin.z},adjusted);
    auto ndc=[](double numerator,double w) { return w>1e-8?numerator/w:std::numeric_limits<double>::quiet_NaN(); };
    char text[1152]{};
    std::snprintf(text,sizeof(text),"Avatar diagnostic: scale=%.1f origin=(%.1f,%.1f,%.1f) verts=%zu tris=%zu clipCandidates=%zu worldY=[%.1f,%.1f] ndcY=[%.3f,%.3f] feetNDC=(%.3f,%.3f,%.3f;w=%.1f) headNDC=(%.3f,%.3f,%.3f;w=%.1f) RT=%p %ux%u fmt=%u BB=%p %ux%u same=%u DS=%p %ux%u fmt=%u VP=(%u,%u,%u,%u,z=%.3f..%.3f) hostDepth=%lu/%lu creationFlags=0x%08lX alphaAtlas=%ux%u sources=%zu",
        p.worldUnitsPerBlock,avatarPlacement.origin.x,avatarPlacement.origin.y,avatarPlacement.origin.z,geometry.vertices,geometry.triangles,geometry.candidates,geometry.minY,geometry.maxY,geometry.minNdcY,geometry.maxNdcY,
        ndc(feet.x,feet.w),ndc(feet.y,feet.w),ndc(feet.z,feet.w),feet.w,ndc(head.x,head.w),ndc(head.y,head.w),ndc(head.z,head.w),head.w,
        static_cast<void*>(target),rt.Width,rt.Height,unsigned(rt.Format),static_cast<void*>(back),bb.Width,bb.Height,unsigned(target==back),static_cast<void*>(depth),ds.Width,ds.Height,unsigned(ds.Format),vp.X,vp.Y,vp.Width,vp.Height,vp.MinZ,vp.MaxZ,gameZ,gameZFunc,creation.BehaviorFlags,impl_->transparentAtlas.width,impl_->transparentAtlas.height,impl_->atlasRegions.size());
    if(target)target->Release();if(back)back->Release();if(depth)depth->Release();
    size_t gpuMaps{},gpuBytes{},gpuMaterials{};for(const auto& item:impl_->textures)if(item.second.gpu){++gpuMaps;gpuBytes+=size_t(item.second.width)*item.second.height*(item.second.format&255);}
    for(const auto& item:impl_->materials)if(item.second.shader)++gpuMaterials;
    size_t deformationVertices{},safeFits{},deformationBytes{};for(const auto& frame:impl_->deformation)for(const auto& batch:frame.batches)deformationVertices+=batch.size();
    if(!impl_->deformation.empty())for(const auto& fit:impl_->deformation.back().fits)safeFits+=fit.safe;
    for(const auto& buffer:impl_->deformationGpu)deformationBytes+=buffer.capacity*buffer.stride;
    return std::string(text)+" materials="+std::to_string(impl_->materials.size())+" gpuMaterials="+std::to_string(gpuMaterials)+" compactBytes="+std::to_string(impl_->textureBytes)+" gpuMaps="+std::to_string(gpuMaps)+" gpuLogicalBytes="+std::to_string(gpuBytes)+" captureEpoch="+std::to_string(impl_->captureEpoch)+" poseEpoch="+std::to_string(impl_->presentationEpoch)+
        " deformationFrames="+std::to_string(impl_->deformation.size())+" deformationSafeFits="+std::to_string(safeFits)+" deformationDelayMs="+std::to_string(impl_->deformationClock.delayMs())+
        " deformationCpuBytes="+std::to_string(deformationVertices*sizeof(VehicleDeformationVertex))+" deformationGpuBytes="+std::to_string(deformationBytes)+" deformationUploads="+std::to_string(impl_->deformationUploads)+" deformationDraws="+std::to_string(impl_->deformationDraws)+(impl_->shaderError.empty()?"":" shaderError="+impl_->shaderError.substr(0,512));
}

bool Renderer::render(IDirect3DDevice9* d,const NativeSnapshot& native,const RenderPlacement& p,bool* worldValid,double presentationMs) {
    if(worldValid)*worldValid=false;
    if (!d || !native.canReplace() || !std::isfinite(p.yawRadians) || !std::isfinite(p.worldUnitsPerBlock) || p.worldUnitsPerBlock<=0 || !std::isfinite(p.ySign) || std::abs(p.ySign)!=1.f || (p.useOrientation&&!validVehicleQuaternion(p.orientation))) return false;
    if(native.rasterProjectionValid)for(const auto& row:native.rasterProjection)for(float value:row)if(!std::isfinite(value))return false;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->retirePendingResources();
    if (impl_->device!=d) { impl_->releaseGpu(); impl_->device=d; }
    if(p.hostEpoch&&p.drawAvatar&&impl_->avatarEpoch!=p.hostEpoch)return false;
    bool drawScene=p.drawScene&&(!p.hostEpoch||impl_->sceneEpoch==p.hostEpoch);
    bool drawDynamic=p.drawDynamic&&(!p.hostEpoch||impl_->dynamicEpoch==p.hostEpoch);
    if(p.drawAvatar&&impl_->avatar.empty())return false;
    // DrawPrimitiveUP success proves command submission, not visible geometry.
    // Reject an entirely clipped avatar so a bad scale/camera cannot hide Leon.
    // The shared-plane test retains large triangles crossing the viewport even
    // when none of their individual vertices lie inside its clip rectangle.
    // A missing/mismatched depth attachment cannot provide world occlusion.
    IDirect3DSurface9* depth{};
    if (FAILED(d->GetDepthStencilSurface(&depth)) || !depth) return false;
    D3DSURFACE_DESC depthDesc{}; HRESULT depthResult=depth->GetDesc(&depthDesc); depth->Release();
    IDirect3DSurface9* target{};if(FAILED(d->GetRenderTarget(0,&target))||!target)return false;
    D3DSURFACE_DESC targetDesc{};HRESULT targetResult=target->GetDesc(&targetDesc);target->Release();
    D3DVIEWPORT9 viewport{};
    if (FAILED(depthResult)||FAILED(targetResult)||FAILED(d->GetViewport(&viewport))||!viewport.Width||!viewport.Height||viewport.X+viewport.Width>depthDesc.Width||viewport.Y+viewport.Height>depthDesc.Height||viewport.X+viewport.Width>targetDesc.Width||viewport.Y+viewport.Height>targetDesc.Height) return false;
    if(worldValid)*worldValid=true;
    if(!p.drawAvatar&&(!drawScene||impl_->scene.empty())&&(!drawDynamic||impl_->dynamic.empty()))return false;
    auto adjusted=viewportCamera(native,viewport);
    auto avatarPlacement=impl_->placement(p,1),scenePlacement=impl_->placement(p,2),dynamicPlacement=impl_->placement(p,3);
    if(presentationMs<0)presentationMs=vehiclePresentationNowMs();
    impl_->drawDeformationFrom=impl_->drawDeformationTo=nullptr;
    bool useDeformation=false;Bounds avatarBounds=impl_->avatarBounds;
    if(impl_->vehicleAssets&&impl_->captureEpoch==impl_->presentationEpoch&&impl_->captureVehicleId&&
       impl_->captureVehicleId==impl_->presentationVehicleId&&impl_->deformation.size()==impl_->deformationClock.count()){
        const auto selection=impl_->deformationClock.sample(presentationMs);
        if(selection.blending&&selection.from<impl_->deformation.size()&&selection.to<impl_->deformation.size()){
            const auto& from=impl_->deformation[selection.from];const auto& to=impl_->deformation[selection.to];
            if(to.fits.size()==impl_->avatar.size()){
                useDeformation=true;impl_->drawDeformationFrom=&from;impl_->drawDeformationTo=&to;
                ++impl_->deformationDraws;
                impl_->deformationTransforms.resize(to.fits.size());
                for(size_t i=0;i<to.fits.size();++i){const auto& fit=to.fits[i];impl_->deformationTransforms[i]={vehicleSlerp({},fit.rotation,selection.fraction),
                    {-fit.rotation.x,-fit.rotation.y,-fit.rotation.z,fit.rotation.w},vehicleLerp(fit.fromCenter,fit.toCenter,selection.fraction),selection.fraction,fit.safe};}
                avatarPlacement.orientation=impl_->presentationOrientation;avatarPlacement.useOrientation=true;
                // Rotation can pass outside both endpoint AABBs. A sphere
                // around the actual local part centers conservatively covers
                // the full observed rotation, not only its endpoint corners.
                avatarBounds=to.bounds;
                for(size_t i=0;i<to.fits.size();++i){const auto& fit=to.fits[i];if(!fit.safe)continue;
                    const auto center=impl_->deformationTransforms[i].center;const float radius=float(fit.radius+fit.maxResidual);
                    avatarBounds.min.x=std::min(avatarBounds.min.x,float(center.x)-radius);avatarBounds.max.x=std::max(avatarBounds.max.x,float(center.x)+radius);
                    avatarBounds.min.y=std::min(avatarBounds.min.y,float(center.y)-radius);avatarBounds.max.y=std::max(avatarBounds.max.y,float(center.y)+radius);
                    avatarBounds.min.z=std::min(avatarBounds.min.z,float(center.z)-radius);avatarBounds.max.z=std::max(avatarBounds.max.z,float(center.z)+radius);}
            }
        }
    }
    // Cache the mesh bounds at ingestion. Eight conservative clip points avoid
    // projecting every nearby block triangle on every native 60 Hz frame.
    const bool drawAvatar=p.drawAvatar&&boundsVisible(avatarBounds,adjusted,avatarPlacement);
    drawScene=drawScene&&boundsVisible(impl_->sceneBounds,adjusted,scenePlacement);
    drawDynamic=drawDynamic&&boundsVisible(impl_->dynamicBounds,adjusted,dynamicPlacement);
    if(!drawAvatar&&!drawScene&&!drawDynamic)return false;
    auto failure=[&](){if(worldValid)*worldValid=false;return false;};
    if(drawAvatar&&(useDeformation?!impl_->ensureDeformationBuffers(d,*impl_->drawDeformationFrom,*impl_->drawDeformationTo):
        !impl_->ensureBuffers(d,impl_->avatar,impl_->avatarGpu,impl_->avatarRevision,impl_->avatarGpuRevision)))return failure();
    if(drawScene&&!impl_->ensureBuffers(d,impl_->scene,impl_->sceneGpu,impl_->sceneRevision,impl_->sceneGpuRevision))return failure();
    if(drawDynamic&&!impl_->ensureBuffers(d,impl_->dynamic,impl_->dynamicGpu,impl_->dynamicRevision,impl_->dynamicGpuRevision))return failure();
    for (const auto& b:impl_->avatar) if (drawAvatar&&b.textureId) {
        auto found=impl_->textures.find(b.textureId);
        if (found==impl_->textures.end() || found->second.rgba.empty() || !impl_->ensureTexture(d,found->second)) return failure();
    }
    StateGuard saved(d);
    if (!saved.good) return failure();
    D3DMATRIX identity{}; for (int n=0;n<4;++n) identity.m[n][n]=1.f;
    D3DMATRIX view=identity,projection{};
    for (int row=0;row<3;++row) for (int column=0;column<4;++column) view.m[column][row]=adjusted.view[row][column];
    // The game's GX projection uses depth [-1,0]. GXSetProjection converts it
    // to Direct3D [0,1]; the equivalent conversion adds clip.w to clip.z.
    for (int row=0;row<4;++row) for (int column=0;column<4;++column) projection.m[column][row]=adjusted.rasterProjectionValid?adjusted.rasterProjection[row][column]:adjusted.projection[row][column]+(row==2?adjusted.projection[3][column]:0.f);
    d->SetVertexShader(nullptr); d->SetPixelShader(nullptr); d->SetFVF(AvatarFVF);
    d->SetTransform(D3DTS_WORLD,&identity); d->SetTransform(D3DTS_VIEW,&view); d->SetTransform(D3DTS_PROJECTION,&projection);
    d->SetRenderState(D3DRS_LIGHTING,FALSE); d->SetRenderState(D3DRS_FOGENABLE,FALSE);
    d->SetRenderState(D3DRS_CLIPPING,TRUE); d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    d->SetRenderState(D3DRS_VERTEXBLEND,D3DVBF_DISABLE); d->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE); d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE); d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL); d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
    d->SetRenderState(D3DRS_DEPTHBIAS,0); d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
    d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE); d->SetRenderState(D3DRS_ALPHAREF,127); d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE); d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA); d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA); d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE); d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE); d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,0xf); d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE); d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE); d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE); d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
    d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0); d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT); d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT); d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP); d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP); d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    bool drawn=false,resourcesGood=true;
    IDirect3DTexture9* lastTexture{};IDirect3DVertexBuffer9* lastStream{};
    const RenderPlacement* lastPlacement{};
    bool textureKnown=false,textureModeKnown=false,lastTextured=false,lastOverlay=false;
    DWORD lastAddress=D3DTADDRESS_CLAMP,lastDepthWrite=TRUE,lastAlphaReference=127;
    Impl::MaterialEntry* lastMaterial{};bool materialActive=false;
    bool lastMorph=false;size_t lastDeformationIndex=size_t(-1);
    DWORD lastFVF=AvatarFVF;
    std::array<IDirect3DBaseTexture9*,16> materialTextures{};
    auto depthAlpha=[&](uint32_t flags){
        const DWORD write=meshMaterialDepthWrite(flags),alpha=meshMaterialAlphaReference(flags);
        if(!impl_->vehicleAssets||write!=lastDepthWrite)d->SetRenderState(D3DRS_ZWRITEENABLE,write);
        if(!impl_->vehicleAssets||alpha!=lastAlphaReference)d->SetRenderState(D3DRS_ALPHAREF,alpha);
        lastDepthWrite=write;lastAlphaReference=alpha;
    };
    D3DCAPS9 caps{};if(FAILED(d->GetDeviceCaps(&caps)))return failure();
    const size_t maxVertices=std::max<size_t>(3,std::min<size_t>(caps.MaxPrimitiveCount,16384)*3);
    auto bindMaterial=[&](Impl::MaterialEntry& m,const RenderPlacement& placement,bool morph,size_t deformationIndex){
        if(!impl_->ensureMaterial(d,m)){resourcesGood=false;return false;}
        if(morph&&!impl_->ensureDeformationShader(d)){resourcesGood=false;return false;}
        if(!materialActive||lastMorph!=morph){d->SetVertexShader(morph?impl_->deformationShader:impl_->vehicleShader);
            if(morph)d->SetVertexDeclaration(impl_->deformationDeclaration);else d->SetFVF(VehicleFVF);
            lastFVF=morph?0:VehicleFVF;lastDeformationIndex=size_t(-1);}
        if(!materialActive||lastMaterial!=&m)d->SetPixelShader(m.shader);
        if(!morph&&lastFVF!=VehicleFVF){d->SetFVF(VehicleFVF);lastFVF=VehicleFVF;}
        if(!materialActive||lastPlacement!=&placement){
            const D3DMATRIX world=placementMatrix(placement);D3DMATRIX worldView{},wvp{};
            for(unsigned row=0;row<4;row++)for(unsigned col=0;col<4;col++)for(unsigned i=0;i<4;i++)worldView.m[row][col]+=world.m[row][i]*view.m[i][col];
            for(unsigned row=0;row<4;row++)for(unsigned col=0;col<4;col++)for(unsigned i=0;i<4;i++)wvp.m[row][col]+=worldView.m[row][i]*projection.m[i][col];
            d->SetVertexShaderConstantF(0,&wvp.m[0][0],4);d->SetVertexShaderConstantF(4,&world.m[0][0],4);
        }
        if(morph&&lastDeformationIndex!=deformationIndex){const auto& transform=impl_->deformationTransforms[deformationIndex];const auto& fit=impl_->drawDeformationTo->fits[deformationIndex];
            float constants[10][4]{};constants[0][0]=float(transform.fraction);
            constants[1][0]=float(fit.fromCenter.x);constants[1][1]=float(fit.fromCenter.y);constants[1][2]=float(fit.fromCenter.z);
            constants[2][0]=float(fit.toCenter.x);constants[2][1]=float(fit.toCenter.y);constants[2][2]=float(fit.toCenter.z);
            for(unsigned i=0;i<3;++i){VehicleVector axis{};if(i==0)axis.x=1;if(i==1)axis.y=1;if(i==2)axis.z=1;
                const auto inverse=vehicleRotate(transform.inverse,axis),partial=vehicleRotate(transform.partial,axis);
                constants[3][i]=float(inverse.x);constants[4][i]=float(inverse.y);constants[5][i]=float(inverse.z);
                constants[6][i]=float(partial.x);constants[7][i]=float(partial.y);constants[8][i]=float(partial.z);}
            constants[9][0]=float(transform.center.x);constants[9][1]=float(transform.center.y);constants[9][2]=float(transform.center.z);
            d->SetVertexShaderConstantF(8,&constants[0][0],10);lastDeformationIndex=deformationIndex;
        }
        if(lastMaterial!=&m||!materialActive){
            float constants[28][4]{};
            // Native view is an orthonormal world-to-camera 3x4 matrix.
            for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)constants[0][i]-=adjusted.view[j][i]*adjusted.view[j][3];
            std::memcpy(constants[1],m.header.paintColor,sizeof(m.header.paintColor));std::memcpy(constants[4],m.header.paintData,sizeof(m.header.paintData));
            for(size_t i=0;i<m.layers.size();i++){const auto& layer=m.layers[i];const size_t c=7+i*5;
                std::memcpy(constants[c],layer.baseColor,sizeof(layer.baseColor));std::memcpy(constants[c+1],layer.emissive,sizeof(layer.emissive));constants[c+1][3]=layer.emissiveNits;
                constants[c+2][0]=layer.opacity;constants[c+2][1]=layer.metallic;constants[c+2][2]=layer.roughness;constants[c+2][3]=layer.ao;
                constants[c+3][0]=layer.clearcoat;constants[c+3][1]=layer.clearcoatRoughness;constants[c+3][2]=layer.normalStrength;constants[c+3][3]=layer.detailNormalStrength;
                constants[c+4][0]=layer.detailScaleX;constants[c+4][1]=layer.detailScaleY;
            }
            constants[27][0]=m.header.alphaRef;d->SetPixelShaderConstantF(0,&constants[0][0],28);
            for(unsigned slot=0;slot<=m.samplerIds.size();slot++){
                IDirect3DBaseTexture9* texture=impl_->environment;bool srgb=false;
                if(slot<m.samplerIds.size()){auto& entry=impl_->textures.at(m.samplerIds[slot]);if(!impl_->ensureTexture(d,entry)){resourcesGood=false;return false;}texture=entry.gpu;srgb=(entry.format&256)!=0;}
                if(!materialActive||materialTextures[slot]!=texture)d->SetTexture(slot,texture);materialTextures[slot]=texture;
                d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,srgb);
            }
            if(!materialActive)for(unsigned slot=0;slot<16;slot++){
                d->SetSamplerState(slot,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(slot,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
                d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
            }
            const unsigned blend=(m.header.flags>>8)&7;const bool translucent=(m.header.flags&2)!=0;
            d->SetRenderState(D3DRS_ZWRITEENABLE,!translucent||(m.header.flags&8));d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
            d->SetRenderState(D3DRS_CULLMODE,(m.header.flags&1)?D3DCULL_NONE:placement.ySign<0?D3DCULL_CCW:D3DCULL_CW);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,translucent&&blend!=0);
            const DWORD sourceBlend=blend==3||blend==5?D3DBLEND_ONE:blend==4?D3DBLEND_DESTCOLOR:D3DBLEND_SRCALPHA;
            const DWORD destBlend=blend==2||blend==3?D3DBLEND_ONE:blend==4?D3DBLEND_ZERO:D3DBLEND_INVSRCALPHA;
            d->SetRenderState(D3DRS_SRCBLEND,sourceBlend);d->SetRenderState(D3DRS_DESTBLEND,destBlend);
            d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_INVSRCALPHA);
        }
        lastMaterial=&m;materialActive=true;lastMorph=morph;lastPlacement=&placement;textureKnown=textureModeKnown=false;return true;
    };
    auto drawBatch=[&](const RenderBatch& batch,const RenderPlacement& placement,const Impl::GpuBatch* gpu,const std::vector<size_t>* sortedTriangles=nullptr,size_t gpuFirst=0,size_t gpuCount=0,IDirect3DTexture9* atlasTexture=nullptr,size_t deformationIndex=size_t(-1)) {
            if (batch.vertices.empty()) return;
            const bool surface=batch.materialId!=0;
            if(surface){auto material=impl_->materials.find(batch.materialId);if(material==impl_->materials.end()||material->second.header.epoch!=impl_->captureEpoch||!bindMaterial(material->second,placement,gpu&&gpu->morph,deformationIndex)){resourcesGood=false;return;}}
            else if(materialActive){
                d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(AvatarFVF);lastFVF=AvatarFVF;lastMaterial=nullptr;materialActive=false;
                d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE);
                d->SetRenderState(D3DRS_ALPHABLENDENABLE,(batch.flags&2)!=0);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
                d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);lastAddress=DWORD(-1);lastPlacement=nullptr;lastStream=nullptr;
                lastDepthWrite=DWORD(-1);lastAlphaReference=DWORD(-1);depthAlpha(batch.flags);
            }
            if(!surface){
            IDirect3DTexture9* texture=atlasTexture;
            if (!texture&&batch.textureId) {
                auto entry=impl_->textures.find(batch.textureId);
                if (entry==impl_->textures.end() || entry->second.rgba.empty() || !impl_->ensureTexture(d,entry->second)){resourcesGood=false;return;}
                texture=entry->second.gpu;
            }
            if(!impl_->vehicleAssets||!textureKnown||texture!=lastTexture)d->SetTexture(0,texture);
            textureKnown=true;lastTexture=texture;
            // Standalone vanilla destroy-stage textures use the default
            // REPEAT sampler. SheetedDecalTextureGenerator emits UV outside
            // 0..1 even for cube faces; atlases keep the existing clamp.
            const DWORD address=(batch.flags&4)?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP;
            if(!impl_->vehicleAssets||address!=lastAddress){d->SetSamplerState(0,D3DSAMP_ADDRESSU,address);d->SetSamplerState(0,D3DSAMP_ADDRESSV,address);}lastAddress=address;
            if(!impl_->vehicleAssets||!textureModeKnown||bool(texture)!=lastTextured){
                d->SetTextureStageState(0,D3DTSS_COLOROP,texture?D3DTOP_MODULATE:D3DTOP_SELECTARG2);
                d->SetTextureStageState(0,D3DTSS_ALPHAOP,texture?D3DTOP_MODULATE:D3DTOP_SELECTARG2);
            }textureModeKnown=true;lastTextured=bool(texture);
            const bool overlay=(batch.flags&16)!=0;
            if((!impl_->vehicleAssets||overlay!=lastOverlay)&&overlay){
                // Actual OverlayTexture(U15,V10) is white with alpha63.
                // entity.fsh mixes white/currentRGB with base weight63/255;
                // overlay processing leaves the texture/vertex alpha intact.
                d->SetRenderState(D3DRS_TEXTUREFACTOR,0x3fffffff);
                d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_LERP);
                d->SetTextureStageState(1,D3DTSS_COLORARG0,D3DTA_TFACTOR|D3DTA_ALPHAREPLICATE);
                d->SetTextureStageState(1,D3DTSS_COLORARG1,D3DTA_CURRENT);d->SetTextureStageState(1,D3DTSS_COLORARG2,D3DTA_TFACTOR);
                d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(1,D3DTSS_ALPHAARG1,D3DTA_CURRENT);
            }else if(!impl_->vehicleAssets||overlay!=lastOverlay){d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);}lastOverlay=overlay;
            const D3DMATRIX world=placementMatrix(placement);
            if(!impl_->vehicleAssets||lastPlacement!=&placement)d->SetTransform(D3DTS_WORLD,&world);lastPlacement=&placement;
            }
            if(gpu&&gpu->vertices&&!sortedTriangles) {
                if(!surface&&lastFVF!=gpu->fvf){d->SetFVF(gpu->fvf);lastFVF=gpu->fvf;}
                if(!impl_->vehicleAssets||lastStream!=gpu->vertices)d->SetStreamSource(0,gpu->vertices,0,gpu->stride);lastStream=gpu->vertices;
                const size_t total=gpuCount?gpuCount:gpu->indices?gpu->indexCount:gpu->count;
                if(gpu->indices){
                    if(gpu->count-1>caps.MaxVertexIndex||FAILED(d->SetIndices(gpu->indices))){resourcesGood=false;return;}
                    for(size_t n=0;n<total;n+=maxVertices){size_t count=std::min(total-n,maxVertices);
                        if(SUCCEEDED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,static_cast<UINT>(gpu->count),static_cast<UINT>(gpuFirst+n),static_cast<UINT>(count/3))))drawn=true;else resourcesGood=false;}
                }else for(size_t n=0;n<total;n+=maxVertices){size_t count=std::min(total-n,maxVertices);if(SUCCEEDED(d->DrawPrimitive(D3DPT_TRIANGLELIST,static_cast<UINT>(gpuFirst+n),static_cast<UINT>(count/3))))drawn=true;else resourcesGood=false;}
                return;
            }
            if(surface){resourcesGood=false;return;} // Surface geometry always uses the UV2 GPU stream.
            auto& vertices=impl_->scratch;vertices.resize(sortedTriangles?sortedTriangles->size()*3:batch.vertices.size());
            for (size_t n=0;n<vertices.size();++n) {
                size_t source=sortedTriangles?(*sortedTriangles)[n/3]*3+n%3:n;
                const auto& v=batch.vertices[triangleVertexIndex(batch,source)]; auto& out=vertices[n];
                out.x=v.x;out.y=v.y;out.z=v.z;out.nx=v.nx;out.ny=v.ny;out.nz=v.nz;
                out.color=v.color; out.u=v.u; out.v=v.v;
            }
            for (size_t n=0;n<vertices.size();n+=maxVertices) {
                const size_t count=std::min(vertices.size()-n,maxVertices);
                if (SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,static_cast<UINT>(count/3),vertices.data()+n,sizeof(FixedVertex)))) drawn=true;else resourcesGood=false;
            }
            lastStream=nullptr; // DrawPrimitiveUP clears stream zero.
    };
    struct TransparentTriangle{const RenderBatch* batch;const RenderPlacement* placement;size_t first;double distance;size_t deformationIndex;};
    std::vector<TransparentTriangle> transparent;
    struct DecalBatch{const RenderBatch* batch;const RenderPlacement* placement;const Impl::GpuBatch* gpu;size_t deformationIndex;};
    std::vector<DecalBatch> decals;
    auto enqueue=[&](const std::vector<RenderBatch>& batches,const std::vector<Impl::GpuBatch>& gpu,const RenderPlacement& placement,bool deformed=false){for(size_t n=0;n<batches.size();++n){const auto& batch=batches[n];
        if(batch.flags&4){decals.push_back({&batch,&placement,&gpu[n],deformed?n:size_t(-1)});continue;}
        if(!(batch.flags&2)){depthAlpha(batch.flags);drawBatch(batch,placement,&gpu[n],nullptr,0,0,nullptr,deformed?n:size_t(-1));continue;}
        for(size_t first=0;first+2<triangleVertexCount(batch);first+=3){double distance=0;for(size_t i=0;i<3;++i){const size_t index=triangleVertexIndex(batch,first+i);Vertex vertex=batch.vertices[index];
                if(deformed){const auto point=impl_->deformationPoint(n,index);vertex.x=float(point.x);vertex.y=float(point.y);vertex.z=float(point.z);}
                Vec3 position=placed(vertex,placement);distance-=adjusted.view[2][0]*position.x+adjusted.view[2][1]*position.y+adjusted.view[2][2]*position.z+adjusted.view[2][3];}
            transparent.push_back({&batch,&placement,first/3,distance/3,deformed?n:size_t(-1)});}
    }};
    // Opaque geometry writes its own depth while retaining existing host depth;
    // this makes front surfaces cover rear body/armor faces and scene blocks.
    if(drawScene)enqueue(impl_->scene,impl_->sceneGpu,scenePlacement);if(drawAvatar)enqueue(impl_->avatar,useDeformation?impl_->deformationGpu:impl_->avatarGpu,avatarPlacement,useDeformation);
    if(drawDynamic)enqueue(impl_->dynamic,impl_->dynamicGpu,dynamicPlacement);
    if(!decals.empty()){
        // Vanilla CRUMBLING uses dstColor/srcColor RGB, ONE/ZERO alpha,
        // no depth write, and reversed-Z bias(scale1,constant10). Native Z is
        // forward: invert the bias and convert depth units to this attachment.
        d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCCOLOR);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_ZERO);d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_ALPHAREF,25);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,floatState(-1.f));d->SetRenderState(D3DRS_DEPTHBIAS,floatState(-10.f*depthResolution(depthDesc.Format)));
        for(const auto& decal:decals)drawBatch(*decal.batch,*decal.placement,decal.gpu,nullptr,0,0,nullptr,decal.deformationIndex);
        d->SetRenderState(D3DRS_DEPTHBIAS,0);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
    }
    std::stable_sort(transparent.begin(),transparent.end(),[](const auto& a,const auto& b){return a.distance>b.distance;});
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
    if(impl_->vehicleAssets&&!transparent.empty()){
        // Keep the exact global alpha order, but upload the sorted stream once.
        // Adjacent triangles with identical material and placement can share a
        // draw even when the source split them into different flexmesh batches.
        bool atlasEligible=impl_->vehicleAtlasEnabled;std::map<uint32_t,bool> atlasIds;
        const auto* atlasPlacement=transparent.front().placement;
        for(const auto& triangle:transparent){
            if(triangle.batch->materialId||triangle.placement!=atlasPlacement||(triangle.batch->flags!=2&&triangle.batch->flags!=3)){atlasEligible=false;break;}
            for(size_t j=0;j<3;j++){const auto& vertex=triangle.batch->vertices[triangleVertexIndex(*triangle.batch,triangle.first*3+j)];
                if(vertex.u<0||vertex.u>1||vertex.v<0||vertex.v>1){atlasEligible=false;break;}
            }
            if(!atlasEligible)break;atlasIds[triangle.batch->textureId]=true;
        }
        std::vector<uint32_t> ids;for(const auto& source:atlasIds)ids.push_back(source.first);
        const bool useAtlas=atlasEligible&&impl_->ensureTransparentAtlas(d,ids);
        const bool surfaceStream=std::any_of(transparent.begin(),transparent.end(),[](const auto& t){return t.batch->materialId!=0;});
        const bool morphStream=surfaceStream&&useDeformation;
        auto& sorted=impl_->scratch;sorted.resize(surfaceStream?0:transparent.size()*3);
        auto& surfaceSorted=impl_->vehicleScratch;surfaceSorted.resize(surfaceStream&&!morphStream?transparent.size()*3:0);
        auto& morphSorted=impl_->morphScratch;morphSorted.resize(morphStream?transparent.size()*3:0);
        for(size_t i=0;i<transparent.size();++i)for(size_t j=0;j<3;++j){const auto& triangle=transparent[i];const size_t source=triangleVertexIndex(*triangle.batch,triangle.first*3+j);const auto& v=triangle.batch->vertices[source];
            if(surfaceStream){const auto uv=triangle.batch->secondaryUV.empty()?std::array<float,2>{v.u,v.v}:triangle.batch->secondaryUV[source];
                FixedVehicleVertex current{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.color,v.u,v.v,uv[0],uv[1]};
                if(morphStream){VehicleDeformationVertex old{v.x,v.y,v.z,v.nx,v.ny,v.nz};
                    if(triangle.deformationIndex!=size_t(-1)){const auto& endpoint=impl_->drawDeformationTo->batches[triangle.deformationIndex][source];old=impl_->drawDeformationFrom->batches[triangle.deformationIndex][source];
                        current.x=endpoint.x;current.y=endpoint.y;current.z=endpoint.z;current.nx=endpoint.nx;current.ny=endpoint.ny;current.nz=endpoint.nz;}
                    morphSorted[i*3+j]={current,old.x,old.y,old.z,old.nx,old.ny,old.nz};}
                else surfaceSorted[i*3+j]=current;continue;}
            auto& out=sorted[i*3+j];out={v.x,v.y,v.z,v.nx,v.ny,v.nz,v.color,v.u,v.v};
            if(useAtlas){const auto& region=impl_->atlasRegions.at(triangle.batch->textureId);
                out.u=(region.x+v.u*region.width)/impl_->transparentAtlas.width;
                out.v=(region.y+v.v*region.height)/impl_->transparentAtlas.height;
            }
        }
        if(morphStream?!impl_->uploadTransparentBytes(d,morphSorted.data(),morphSorted.size(),sizeof(FixedMorphVehicleVertex),VehicleFVF):
            surfaceStream?!impl_->uploadTransparentBytes(d,surfaceSorted.data(),surfaceSorted.size(),sizeof(FixedVehicleVertex),VehicleFVF):!impl_->uploadTransparent(d,sorted))return failure();
        if(useAtlas){depthAlpha(transparent.front().batch->flags);drawBatch(*transparent.front().batch,*atlasPlacement,&impl_->transparentGpu,nullptr,0,sorted.size(),impl_->transparentAtlas.gpu);}
        else for(size_t first=0;first<transparent.size();){const size_t start=first;const auto* batch=transparent[first].batch;const auto* placement=transparent[first].placement;
            const size_t deformationIndex=transparent[first].deformationIndex;
            do{++first;}while(first<transparent.size()&&transparent[first].placement==placement&&transparent[first].batch->textureId==batch->textureId&&transparent[first].batch->flags==batch->flags&&transparent[first].batch->materialId==batch->materialId&&(!morphStream||transparent[first].deformationIndex==deformationIndex));
            impl_->transparentGpu.morph=morphStream&&deformationIndex!=size_t(-1)&&impl_->deformationTransforms[deformationIndex].safe;
            depthAlpha(batch->flags);drawBatch(*batch,*placement,&impl_->transparentGpu,nullptr,start*3,(first-start)*3,nullptr,deformationIndex);
        }
    }else{
        for(size_t first=0;first<transparent.size();){const auto* batch=transparent[first].batch;const auto* placement=transparent[first].placement;std::vector<size_t> triangles;do{triangles.push_back(transparent[first++].first);}while(first<transparent.size()&&transparent[first].batch==batch);d->SetRenderState(D3DRS_ZWRITEENABLE,meshMaterialDepthWrite(batch->flags));d->SetRenderState(D3DRS_ALPHAREF,meshMaterialAlphaReference(batch->flags));drawBatch(*batch,*placement,nullptr,&triangles);}
    }
    if(!resourcesGood&&worldValid)*worldValid=false;
    return drawn;
}
}
