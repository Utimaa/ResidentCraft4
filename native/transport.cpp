#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "transport.hpp"
#include "mesh_material.hpp"
#include "beamng_ui_reader.hpp"
#include "vehicle_presentation.hpp"
#include "texture_queue_budget.hpp"
#include "vehicle_texture_storage.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

namespace re4craft {
namespace {
// Packet decoding borrows the bounded receive buffer only for this call. Every
// published mailbox owns its data, so the network can reuse that buffer at once.
struct PacketBytes {
    const uint8_t* pointer{};size_t count{};
    const uint8_t* data() const{return pointer;}
    size_t size() const{return count;}
    bool empty() const{return !count;}
    const uint8_t* begin() const{return pointer;}
    const uint8_t* end() const{return count?pointer+count:pointer;}
};
std::string rejectedPacket(Message type,uint32_t sequence,PacketBytes payload,bool vehiclePeer) {
    // Only emit on a rejected local game packet. Keep both field diagnostics
    // and raw evidence bounded; the parser and connection policy stay strict.
    char fields[384]{};
    if(type==Message::State&&payload.size()==sizeof(State)) {
        State s{};std::memcpy(&s,payload.data(),sizeof(s));
        std::snprintf(fields,sizeof(fields)," state flags=%u slot=%u camera=%u reserved=%u xyz=(%.9g,%.9g,%.9g) yaw/pitch=(%.9g,%.9g) health=(%.9g,%.9g) food/saturation=(%.9g,%.9g) limb=(%.9g,%.9g)",s.flags,s.selectedSlot,s.cameraMode,s.reserved,s.x,s.y,s.z,double(s.yaw),double(s.pitch),double(s.health),double(s.maxHealth),double(s.food),double(s.saturation),double(s.limbSwing),double(s.limbAmount));
    } else if(type==Message::Motion&&payload.size()==sizeof(GuestMotion)) {
        GuestMotion m{};std::memcpy(&m,payload.data(),sizeof(m));
        std::snprintf(fields,sizeof(fields)," motion epoch=%u flags=%u camera=%u reserved=%u xyz=(%.9g,%.9g,%.9g) cameraXYZ=(%.9g,%.9g,%.9g) velocity=(%.9g,%.9g,%.9g) yaw/pitch=(%.9g,%.9g) health=(%.9g,%.9g) roll/fov=(%.9g,%.9g)",m.epoch,m.flags,m.cameraMode,m.reserved,m.hostX,m.hostY,m.hostZ,m.cameraX,m.cameraY,m.cameraZ,double(m.velocityX),double(m.velocityY),double(m.velocityZ),double(m.yaw),double(m.pitch),double(m.health),double(m.maxHealth),double(m.cameraRoll),double(m.fov));
    } else if(type==Message::VehicleInfo&&payload.size()==sizeof(VehicleInfo)) {
        VehicleInfo v{};std::memcpy(&v,payload.data(),sizeof(v));
        std::snprintf(fields,sizeof(fields)," vehicle epoch=%u id=%u nodes=%u flags=%u radius=%.9g reserved=%u",v.epoch,v.vehicleId,v.nodeCount,v.flags,double(v.collisionRadius),v.reserved);
    }
    constexpr char digits[]="0123456789ABCDEF";char prefix[321]{};
    const size_t count=std::min<size_t>(payload.size(),160);
    for(size_t i=0;i<count;++i){prefix[i*2]=digits[payload.data()[i]>>4];prefix[i*2+1]=digits[payload.data()[i]&15];}
    char line[1024]{};
    std::snprintf(line,sizeof(line),"Rejected malformed %s packet: type=%u sequence=%u bytes=%zu%s prefix[%zu]=%s",vehiclePeer?"BeamNG":"Minecraft",unsigned(type),sequence,payload.size(),fields,count,prefix);
    return line;
}
template<class Bytes,class T> bool read(const Bytes& bytes,size_t& at,T& out) {
    if(at>bytes.size() || sizeof(T)>bytes.size()-at)return false;
    std::memcpy(&out,bytes.data()+at,sizeof(T)); at+=sizeof(T); return true;
}
bool validState(const State& s) {
    return std::isfinite(s.x)&&std::isfinite(s.y)&&std::isfinite(s.z)&&
        std::isfinite(s.yaw)&&std::isfinite(s.pitch)&&std::isfinite(s.health)&&
        std::isfinite(s.maxHealth)&&std::isfinite(s.food)&&std::isfinite(s.saturation)&&
        std::isfinite(s.limbSwing)&&std::isfinite(s.limbAmount)&&s.selectedSlot<9&&s.cameraMode<=2&&s.reserved==0&&(s.flags&~127u)==0;
}
bool validMotion(const GuestMotion& m){
    const double positions[]{m.hostX,m.hostY,m.hostZ,m.cameraX,m.cameraY,m.cameraZ};
    for(double value:positions)if(!std::isfinite(value)||std::abs(value)>30000000.)return false;
    const float values[]{m.velocityX,m.velocityY,m.velocityZ,m.yaw,m.pitch,m.health,m.maxHealth,m.cameraRoll,m.fov};
    for(float value:values)if(!std::isfinite(value))return false;
    const bool traversalAck=(m.flags&MotionNativeTraversalAck)!=0;
    if(traversalAck&&((m.flags&(MotionReady|MotionDead|MotionScreen))||!(m.flags&MotionCameraReady)||m.health<=0))return false;
    return (m.flags&~63u)==0&&m.reserved==0&&m.cameraMode<=2&&std::abs(m.pitch)<=90&&m.health>=0&&m.maxHealth>0&&m.health<=m.maxHealth&&m.maxHealth<=10000&&m.fov>=20&&m.fov<=150&&std::abs(m.velocityX)<=256&&std::abs(m.velocityY)<=256&&std::abs(m.velocityZ)<=256;
}
bool validMelee(const GuestMelee& m,bool vehiclePeer){
    const double pos[]{m.attackX,m.attackY,m.attackZ};for(double value:pos)if(!std::isfinite(value)||std::abs(value)>30000000.)return false;
    const float dir[]{m.directionX,m.directionY,m.directionZ};for(float value:dir)if(!std::isfinite(value)||std::abs(value)>1.01f)return false;
    const auto environment=m.flags&(HitHazard|HitExplosion);
    if(environment==unsigned(HitHazard|HitExplosion)||(environment&&(m.flags&(HitCritical|HitProjectile))))return false;
    if((m.flags&HitAlly)&&m.flags!=HitAlly)return false;
    if((m.flags&HitVehicle)&&(!vehiclePeer||m.flags!=HitVehicle))return false;
    if(vehiclePeer&&m.flags!=HitVehicle)return false;
    const float reach=(m.flags&HitExplosion)?16.f:(m.flags&HitAlly)?3.f:(m.flags&(HitHazard|HitProjectile|HitVehicle))?1.f:6.f;
    return m.targetId&&m.eventId&&(m.flags&~127u)==0&&std::isfinite(m.damage)&&m.damage>0&&m.damage<=1024&&std::isfinite(m.knockback)&&m.knockback>=0&&m.knockback<=16&&std::isfinite(m.maxReach)&&m.maxReach>0&&m.maxReach<=reach;
}
bool validVertex(const Vertex& v) {
    const float numbers[]{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v};
    for(float f:numbers)if(!std::isfinite(f)||std::abs(f)>1.0e6f)return false;
    return true;
}
bool newerSerial(uint32_t candidate,uint32_t reference){
    return candidate!=reference&&uint32_t(candidate-reference)<0x80000000u;
}
bool validQuaternion(const float (&q)[4]) {
    double length=0;for(float x:q){if(!std::isfinite(x)||std::abs(x)>1.01f)return false;length+=double(x)*x;}
    return std::abs(length-1.)<.01;
}
bool validVehiclePose(const VehiclePose& p) {
    if(!p.epoch||!p.vehicleId||!std::isfinite(p.sourceSeconds)||p.sourceSeconds<0||p.sourceSeconds>1e10
       ||!validQuaternion(p.quaternion)||!validQuaternion(p.cameraQuaternion)||p.cameraMode>2
       ||!std::isfinite(p.fov)||p.fov<20||p.fov>140)return false;
    for(double v:{p.hostX,p.hostY,p.hostZ,p.cameraX,p.cameraY,p.cameraZ})
        if(!std::isfinite(v)||std::abs(v)>100000.)return false;
    return true;
}
bool validVehicleMaterial(const VehicleMaterial& h,const std::vector<VehicleMaterialLayer>& layers) {
    if(!h.epoch||!h.id||h.id>256||h.layerCount<1||h.layerCount>4||h.layerCount!=layers.size()
       ||h.reserved||(h.flags&~0x71Fu)||((h.flags>>8)>5)||!std::isfinite(h.alphaRef)||h.alphaRef<0||h.alphaRef>1)return false;
    for(const auto& color:h.paintColor)for(float value:color)if(!std::isfinite(value)||value<0||value>64)return false;
    for(const auto& data:h.paintData)for(float value:data)if(!std::isfinite(value)||value<0||value>1)return false;
    for(const auto& l:layers){
        if((l.flags&~127u)||(l.mapUV1Mask&~2047u)||l.reserved)return false;
        for(uint32_t id:l.maps)if(id>65535)return false;
        for(float value:l.baseColor)if(!std::isfinite(value)||value<0||value>64)return false;
        for(float value:l.emissive)if(!std::isfinite(value)||value<0||value>64)return false;
        if(!std::isfinite(l.emissiveNits)||l.emissiveNits<0||l.emissiveNits>100000)return false;
        for(float value:{l.opacity,l.metallic,l.roughness,l.ao,l.clearcoat,l.clearcoatRoughness})
            if(!std::isfinite(value)||value<0||value>1)return false;
        for(float value:{l.normalStrength,l.detailNormalStrength})if(!std::isfinite(value)||value<0||value>16)return false;
        for(float value:{l.detailScaleX,l.detailScaleY})if(!std::isfinite(value)||std::abs(value)>1024)return false;
    }
    return true;
}
bool validVehicleDashboard(const VehicleDashboard& d) {
    if(!d.epoch||!d.vehicleId||!std::isfinite(d.sourceSeconds)||d.sourceSeconds<0||d.sourceSeconds>1e10
       ||(d.flags&~0xff7fu)||d.gearIndex<-16||d.gearIndex>32)return false;
    for(uint32_t value:d.reserved)if(value)return false;
    for(float value:{d.speed,d.rpm,d.maxRPM,d.fuel,d.fuelCapacity,d.damage})if(!std::isfinite(value)||value<0)return false;
    return d.speed<=2048&&d.rpm<=60000&&d.maxRPM<=60000&&d.fuelCapacity<=10000&&d.fuel<=d.fuelCapacity
        &&std::isfinite(d.engineTemperature)&&d.engineTemperature>=-100&&d.engineTemperature<=1000&&d.damage<=1e12f;
}
constexpr uint32_t MaxVehicleMeshBytes=sizeof(Mesh)+MaxBatches*sizeof(IndexedBatch)+MaxVertices*(sizeof(Vertex)+sizeof(uint32_t));
bool indexedVehicleMesh(PacketBytes payload,uint32_t epoch,uint32_t frame,Mesh& mesh,std::vector<MeshBatch>& batches,bool surface=false,VehicleMeshCapture* capture=nullptr){
    size_t at=0;
    if(!read(payload,at,mesh)||mesh.category!=1||mesh.epoch!=epoch||mesh.frame!=frame
       ||!mesh.batchCount||mesh.batchCount>(surface?MaxVehicleBatches:MaxBatches)
       ||payload.size()>(surface?MaxVehicleSurfaceBytes:MaxVehicleMeshBytes))return false;
    for(double value:{mesh.hostOriginX,mesh.hostOriginY,mesh.hostOriginZ})
        if(!std::isfinite(value)||std::abs(value)>30000000.)return false;
    if(surface&&(!capture||!read(payload,at,*capture)||!std::isfinite(capture->sourceSeconds)
       ||capture->sourceSeconds<0||capture->sourceSeconds>1e10||!validQuaternion(capture->quaternion)))return false;
    const size_t batchStart=at;
    uint64_t paletteCount=0,triangleIndices=0;
    // Preflight the entire owned body before allocating any output palette or
    // index vector. A late invalid batch/index cannot leave a partial frame or
    // trigger its output allocation before the decoder rejects the packet.
    for(uint32_t i=0;i<mesh.batchCount;i++){
        IndexedBatch batch{};
        if(!read(payload,at,batch)||!batch.vertexCount||!batch.indexCount||batch.indexCount%3
           ||batch.vertexCount>batch.indexCount
           ||!validMeshMaterial(batch.flags))return false;
        uint32_t materialId=0;
        if(surface&&(!read(payload,at,materialId)||!materialId||materialId>256))return false;
        paletteCount+=batch.vertexCount;triangleIndices+=batch.indexCount;
        if(paletteCount>(surface?MaxVehicleVertices:MaxVertices)
           ||triangleIndices>(surface?MaxVehicleIndices:MaxVertices))return false;
        const size_t sourceVertexSize=surface?sizeof(VehicleVertex):sizeof(Vertex);
        const size_t vertexBytes=size_t(batch.vertexCount)*sourceVertexSize,indexBytes=size_t(batch.indexCount)*sizeof(uint32_t);
        if(at>payload.size()||vertexBytes>payload.size()-at||indexBytes>payload.size()-at-vertexBytes)return false;
        // Validate each actual source vertex once; repeated triangle indices
        // preserve its exact bytes without repeating all attribute checks.
        for(uint32_t vertex=0;vertex<batch.vertexCount;vertex++){
            if(surface){VehicleVertex v{};if(!read(payload,at,v)||!validVertex({v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v,v.color})
                ||!std::isfinite(v.u2)||!std::isfinite(v.v2)||std::abs(v.u2)>1e6f||std::abs(v.v2)>1e6f)return false;}
            else {Vertex value{};if(!read(payload,at,value)||!validVertex(value))return false;}
        }
        for(uint32_t n=0;n<batch.indexCount;++n){
            uint32_t index{};if(!read(payload,at,index)||index>=batch.vertexCount)return false;
        }
    }
    if(at!=payload.size())return false;

    batches.reserve(mesh.batchCount);at=batchStart;
    for(uint32_t i=0;i<mesh.batchCount;++i){
        IndexedBatch batch{};if(!read(payload,at,batch))return false;
        uint32_t materialId=0;if(surface&&!read(payload,at,materialId))return false;
        MeshBatch output{batch.textureId,batch.flags,{},materialId};
        if(surface){
            // Preserve every validated source attribute once. Triangle reuse
            // remains indexed through rendering, rather than manufacturing an
            // expanded palette and carrying a second source-index stream.
            output.vertices.resize(batch.vertexCount);output.secondaryUV.resize(batch.vertexCount);
            for(uint32_t n=0;n<batch.vertexCount;++n){
                VehicleVertex v{};if(!read(payload,at,v))return false;
                output.vertices[n]={v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v,v.color};
                output.secondaryUV[n]={v.u2,v.v2};
            }
            output.indices.resize(batch.indexCount);
            const size_t indexBytes=size_t(batch.indexCount)*sizeof(uint32_t);
            std::memcpy(output.indices.data(),payload.data()+at,indexBytes);at+=indexBytes;
        }else{
            // Type10 retains its original expansion and 300k triangle-vertex
            // budget; generic Minecraft packets are decoded elsewhere.
            const uint8_t* palette=payload.data()+at;at+=size_t(batch.vertexCount)*sizeof(Vertex);
            output.vertices.resize(batch.indexCount);
            for(auto& value:output.vertices){
                uint32_t index{};if(!read(payload,at,index))return false;
                std::memcpy(&value,palette+size_t(index)*sizeof(Vertex),sizeof(value));
            }
        }
        batches.push_back(std::move(output));
    }
    return true;
}
bool sendAll(SOCKET socket,const uint8_t* bytes,size_t size) {
    while(size) {int n=send(socket,reinterpret_cast<const char*>(bytes),static_cast<int>(size),0);
        if(n<=0)return false; bytes+=n; size-=n;} return true;
}
bool decodedHud(PacketBytes payload,HudData& data){
    size_t at=0;Hud h{};if(!read(payload,at,h)||!h.width||!h.height||h.width>1920||h.height>1080||(h.flags&~3u))return false;
    const uint64_t count=uint64_t(h.width)*h.height*4;
    std::vector<uint8_t> pixels;
    if(h.flags&2){
        pixels.resize(static_cast<size_t>(count));size_t filled=0;
        while(at<payload.size()){
            uint32_t token{};if(!read(payload,at,token))return false;
            size_t run=token&0x7fffffff;if(!run||run>(pixels.size()-filled)/4)return false;
            if(token&0x80000000){uint32_t pixel{};if(!read(payload,at,pixel))return false;
                for(size_t i=0;i<run;i++)std::memcpy(pixels.data()+filled+i*4,&pixel,4);
            }else{size_t bytes=run*4;if(bytes>payload.size()-at)return false;std::memcpy(pixels.data()+filled,payload.data()+at,bytes);at+=bytes;}
            filled+=run*4;
        }
        if(filled!=pixels.size())return false;
    }else{if(count!=payload.size()-at)return false;pixels.assign(payload.begin()+at,payload.end());}
    data={h.width,h.height,h.flags&1u,h.frame,std::move(pixels)};return true;
}
}
void Transport::start(uint16_t port) {
    if(thread_.joinable())return;
    stopping_=false; thread_=std::thread([this,port]{run(port);});
}
void Transport::stop(){stopping_=true;if(thread_.joinable())thread_.join();reset();}
void Transport::reset(bool releaseInput){
    connected_=false;
    std::lock_guard lock(mutex_);
    ++uiGeneration_;
    ++textureSession_;textureSerial_=0;latestTextureSerial_.clear();
    vehiclePoseAccepted_=vehiclePoseIgnored_=0;
    stateTime_={};meshTime_={};hudTime_={};motionTime_={};vehicleTime_={};vehiclePoseTime_={};vehicleDashboardTime_={};vehicleDashboard_={};newVehicleDashboard_=false;vehicleMaterials_.clear();vehiclePose_={};vehiclePoses_.clear();meshCapture_={};surfaceMesh_=false;mesh_.clear();scene_.clear();entities_.clear();hud_={};motion_={};vehicle_={};blocks_.clear();melee_.clear();textures_.clear();partialTextures_.clear();partialVehicleMesh_={};outgoing_.clear();newMesh_=newScene_=newEntities_=newHud_=newMotion_=newBlocks_=false;newSession_=true;input_.pressed=0;input_.scroll=0;
    // Empty vectors still retain their allocations after clear(). Returning
    // their capacities is essential when a 2GiB host has rejected an image.
    std::vector<MeshBatch>().swap(mesh_);std::vector<MeshBatch>().swap(scene_);std::vector<MeshBatch>().swap(entities_);
    std::vector<VehiclePoseUpdate>().swap(vehiclePoses_);std::vector<BlockBox>().swap(blocks_);std::vector<GuestMelee>().swap(melee_);
    state_={};meshHeader_={};sceneHeader_={};entityHeader_={};blockEpoch_=0;
    if(releaseInput){const auto epoch=input_.hostEpoch;input_={};input_.hostEpoch=epoch;input_.selectedSlot=UINT32_MAX;inputTime_={};}
}
bool Transport::sendPacket(Message type,std::vector<uint8_t> payload){
    if(!connected_||payload.size()>MaxPayload)return false;
    if(type!=Message::KeyEvent&&type!=Message::Text&&type!=Message::Collision&&type!=Message::MovingSurfaces&&type!=Message::Enemies&&type!=Message::Damage&&type!=Message::Loot)return false;
    if(type==Message::Loot){
        HostLoot loot{};size_t at=0;
        if(payload.size()!=sizeof(loot)||!read(payload,at,loot)||!loot.epoch||!loot.eventId||!loot.sessionId
           ||loot.source<1||loot.source>2||loot.tier>5||!std::isfinite(loot.x)||!std::isfinite(loot.y)||!std::isfinite(loot.z)
           ||std::abs(loot.x)>30000000.||std::abs(loot.y)>30000000.||std::abs(loot.z)>30000000.)return false;
    }
    std::lock_guard lock(mutex_);
    if(type==Message::Collision||type==Message::MovingSurfaces||type==Message::Enemies){for(auto it=outgoing_.begin();it!=outgoing_.end();)if(it->type==type)it=outgoing_.erase(it);else ++it;}
    if(outgoing_.size()>=256)return false;
    outgoing_.push_back({type,std::move(payload)});return true;
}
void Transport::setInput(const Input& input){
    std::lock_guard lock(mutex_);
    if(vehiclePeer_&&(input.hostEpoch!=input_.hostEpoch||bool(input.flags&Enabled)!=bool(input_.flags&Enabled))){
        ++uiGeneration_;hud_={};hudTime_={};newHud_=false;
    }
    if(!(input.flags&Enabled)){
        input_=input;input_.held=input_.pressed=input_.mouseButtons=0;input_.scroll=0;
        for(auto it=outgoing_.begin();it!=outgoing_.end();)
            if(it->type==Message::KeyEvent||it->type==Message::Text)it=outgoing_.erase(it);else ++it;
        inputTime_=std::chrono::steady_clock::now();return;
    }
    // Preserve key edges until a send consumes them, even when render is faster than TCP.
    uint32_t pending=input_.pressed;int64_t wheel=int64_t(input_.scroll)+input.scroll;
    input_=input;input_.pressed|=pending;input_.scroll=static_cast<int32_t>(std::clamp<int64_t>(wheel,-7680,7680));inputTime_=std::chrono::steady_clock::now();
}
Updates Transport::takeUpdates(){
    Updates u;
    std::lock_guard lock(mutex_);
    auto now=std::chrono::steady_clock::now();
    auto age=[&](auto when)->uint32_t {if(!when.time_since_epoch().count())return 0xffffffff;return static_cast<uint32_t>(std::clamp<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now-when).count(),0,0xfffffffe));};
    u.stateAgeMs=age(stateTime_);u.meshAgeMs=age(meshTime_);u.hudAgeMs=age(hudTime_);u.motionAgeMs=age(motionTime_);
    u.connected=connected_;u.state=state_;
    u.stateValid=u.connected && stateTime_.time_since_epoch().count()!=0 && now-stateTime_<std::chrono::milliseconds(750);
    u.meshValid=u.connected && meshTime_.time_since_epoch().count()!=0 && now-meshTime_<std::chrono::milliseconds(1000);
    u.newSession=newSession_;newSession_=false;
    u.newMesh=newMesh_;newMesh_=false;
    if(u.newMesh){u.mesh=std::move(mesh_);u.meshHeader=meshHeader_;u.meshCapture=meshCapture_;u.surfaceMesh=surfaceMesh_;}
    u.newScene=newScene_;newScene_=false;if(u.newScene){u.scene=std::move(scene_);u.sceneHeader=sceneHeader_;}
    u.newEntities=newEntities_;newEntities_=false;if(u.newEntities){u.entities=std::move(entities_);u.entityHeader=entityHeader_;}
    u.newHud=newHud_;newHud_=false;if(u.newHud)u.hud=std::move(hud_);
    u.hudValid=u.connected&&hudTime_.time_since_epoch().count()!=0&&now-hudTime_<std::chrono::milliseconds(750);
    u.hudUiModeKnown=hud_.uiModeKnown;u.hudUiScreenOpen=hud_.uiScreenOpen;
    u.motion=motion_;u.motionValid=u.connected&&motionTime_.time_since_epoch().count()!=0&&now-motionTime_<std::chrono::milliseconds(250);
    u.vehicle=vehicle_;u.vehicleValid=vehiclePeer_&&u.connected&&vehicleTime_.time_since_epoch().count()!=0&&now-vehicleTime_<std::chrono::milliseconds(250);
    u.vehiclePoseValid=vehiclePeer_&&u.connected&&vehiclePoseTime_.time_since_epoch().count()!=0&&now-vehiclePoseTime_<std::chrono::milliseconds(250);
    u.vehicleAgeMs=age(vehicleTime_);u.vehiclePoseAgeMs=age(vehiclePoseTime_);
    u.latestVehiclePose=vehiclePose_;u.vehiclePoseAccepted=vehiclePoseAccepted_;u.vehiclePoseIgnored=vehiclePoseIgnored_;
    u.vehiclePoses=std::move(vehiclePoses_);vehiclePoses_.clear();
    u.vehicleDashboard=vehicleDashboard_;u.newVehicleDashboard=newVehicleDashboard_;newVehicleDashboard_=false;
    u.vehicleDashboardValid=vehiclePeer_&&u.connected&&vehicleDashboardTime_.time_since_epoch().count()!=0&&now-vehicleDashboardTime_<std::chrono::milliseconds(750);
    for(auto& pair:vehicleMaterials_)u.vehicleMaterials.push_back(std::move(pair.second));vehicleMaterials_.clear();
    u.newMotion=newMotion_;newMotion_=false;u.newBlocks=newBlocks_;newBlocks_=false;
    if(u.newBlocks){u.blocks=std::move(blocks_);u.blockEpoch=blockEpoch_;}
    u.melee=std::move(melee_);melee_.clear();
    for(auto& pair:textures_)u.textures.push_back(std::move(pair.second));
    textures_.clear(); return u;
}
bool Transport::accept(Message type,const std::vector<uint8_t>& payload){
    return acceptBytes(type,payload.data(),payload.size());
}
bool Transport::acceptBytes(Message type,const uint8_t* source,size_t size){
    const PacketBytes payload{source,size};
    if(payload.size()>MaxPayload)return false;
    size_t at=0;
    if(type==Message::State){
        State s{};if(payload.size()!=sizeof(s)||!read(payload,at,s)||!validState(s))return false;
        std::lock_guard lock(mutex_);state_=s;stateTime_=std::chrono::steady_clock::now();return true;
    }
    if(type==Message::Texture){
        Texture t{};if(!read(payload,at,t)||t.id==0||t.width==0||t.height==0||t.width>2048||t.height>2048)return false;
        uint64_t bytes=uint64_t(t.width)*t.height*4;
        if(bytes!=payload.size()-at || bytes>MaxPayload-sizeof(t))return false;
        TextureData data{t.id,t.width,t.height,{payload.begin()+at,payload.end()}};
        std::unique_lock lock(mutex_);
        if(partialTextures_.count(t.id)||(vehiclePeer_&&textures_.size()>=256&&!textures_.count(t.id)))return false;
        if(compactVehicleTextures_){
            if(!latestTextureSerial_.count(t.id)&&latestTextureSerial_.size()>=256)return false;
            const auto session=textureSession_,serial=++textureSerial_;
            latestTextureSerial_[t.id]=serial;
            lock.unlock();if(textureCompressionCheck_)textureCompressionCheck_();std::vector<uint8_t> packed;
            if(compressedTexture(data.rgba,packed)){data.rgba=std::move(packed);data.compressed=true;}
            lock.lock();const auto latest=latestTextureSerial_.find(t.id);
            if(session!=textureSession_||latest==latestTextureSerial_.end()||latest->second!=serial)return true;
            latestTextureSerial_.erase(latest);
        }
        size_t queued=0;for(const auto& pair:textures_)queued+=pair.second.rgba.size();
        if(textures_.count(t.id))queued-=textures_[t.id].rgba.size();
        for(const auto& pair:partialTextures_)queued+=pair.second.image.rgba.size();
        const size_t limit=(vehiclePeer_?384u:64u)*1024u*1024u;
        if(queued+data.rgba.size()>limit)return false;
        textures_[t.id]=std::move(data);return true;
    }
    if(type==Message::TextureChunk||type==Message::VehicleTextureChunk){
        TextureChunk chunk{};uint32_t format=4;
        if(type==Message::VehicleTextureChunk){
            VehicleTextureChunk compact{};if(!read(payload,at,compact)||(compact.format!=1&&compact.format!=2&&compact.format!=4&&compact.format!=260))return false;
            format=compact.format;chunk={compact.id,compact.width,compact.height,compact.offset,compact.byteCount,compact.reserved};
            if(compact.byteCount>65536)return false;
        }else if(!read(payload,at,chunk))return false;
        if(!vehiclePeer_||!chunk.id||!chunk.width||!chunk.height
           ||chunk.width>4096||chunk.height>4096||chunk.reserved||!chunk.byteCount
           ||chunk.byteCount>1024u*1024u||chunk.byteCount!=payload.size()-at)return false;
        const uint64_t bytes=uint64_t(chunk.width)*chunk.height*(format&255u);
        if(bytes>64u*1024u*1024u||uint64_t(chunk.offset)+chunk.byteCount>bytes)return false;
        std::unique_lock lock(mutex_);
        auto found=partialTextures_.find(chunk.id);
        if(found==partialTextures_.end()){
            if(chunk.offset)return false;
            size_t completeBytes=0,partialBytes=0;
            for(const auto& pair:textures_)completeBytes+=pair.second.rgba.size();
            for(const auto& pair:partialTextures_)partialBytes+=pair.second.image.rgba.size();
            const auto previous=textures_.find(chunk.id);const bool retiring=previous!=textures_.end();
            const size_t retiredBytes=retiring?previous->second.rgba.size():0;
            if(!vehicleTextureRevisionFits(completeBytes,partialBytes,textures_.size(),partialTextures_.size(),retiredBytes,retiring,static_cast<size_t>(bytes)))return false;
            // All prefix/range/budget validation passed. Retire only the old
            // undrained same-ID candidate before allocating its replacement;
            // no old candidate can leak through takeUpdates during this partial.
            // The renderer independently retains any already uploaded revision.
            if(compactVehicleTextures_&&!latestTextureSerial_.count(chunk.id)&&latestTextureSerial_.size()>=256)return false;
            if(retiring)textures_.erase(previous);
            if(compactVehicleTextures_){
                latestTextureSerial_[chunk.id]=++textureSerial_;
            }
            PartialTexture partial;partial.image={chunk.id,chunk.width,chunk.height,{},format};
            if(textureAllocationCheck_)textureAllocationCheck_(static_cast<size_t>(bytes));
            partial.image.rgba.resize(static_cast<size_t>(bytes));
            found=partialTextures_.emplace(chunk.id,std::move(partial)).first;
        }
        auto& partial=found->second;
        if(partial.received!=chunk.offset||partial.image.width!=chunk.width||partial.image.height!=chunk.height||partial.image.format!=format)return false;
        std::memcpy(partial.image.rgba.data()+chunk.offset,payload.data()+at,chunk.byteCount);
        partial.received+=chunk.byteCount;
        if(partial.received==bytes){
            TextureData completed=std::move(partial.image);partialTextures_.erase(found);
            if(!compactVehicleTextures_){textures_[chunk.id]=std::move(completed);return true;}
            const auto session=textureSession_,serial=latestTextureSerial_.at(chunk.id);
            // CPU compression runs on the receiving worker without holding
            // the mailbox lock that the native rendering thread consumes.
            lock.unlock();if(textureCompressionCheck_)textureCompressionCheck_();std::vector<uint8_t> packed;
            if(compressedTexture(completed.rgba,packed)){completed.rgba=std::move(packed);completed.compressed=true;}
            lock.lock();
            const auto latest=latestTextureSerial_.find(chunk.id);
            if(session!=textureSession_||latest==latestTextureSerial_.end()||latest->second!=serial)return true;
            latestTextureSerial_.erase(latest);
            size_t queued{};for(const auto& item:textures_)if(item.first!=chunk.id)queued+=item.second.rgba.size();
            for(const auto& item:partialTextures_)queued+=item.second.image.rgba.size();
            if(queued+completed.rgba.size()>MaxVehicleTextureStorageBytes)return false;
            textures_[chunk.id]=std::move(completed);
        }
        return true;
    }
    if(type==Message::VehicleMeshChunk||type==Message::VehicleSurfaceChunk){
        const bool surface=type==Message::VehicleSurfaceChunk;
        VehicleMeshChunk chunk{};
        if(!vehiclePeer_||!read(payload,at,chunk)||!chunk.epoch||chunk.reserved||!chunk.byteCount
           ||chunk.byteCount>64u*1024u||chunk.byteCount!=payload.size()-at
           ||chunk.totalBytes<sizeof(Mesh)+(surface?sizeof(VehicleMeshCapture)+sizeof(IndexedVehicleBatch)+sizeof(VehicleVertex):sizeof(IndexedBatch)+sizeof(Vertex))+3*sizeof(uint32_t)
           ||chunk.totalBytes>(surface?MaxVehicleSurfaceBytes:MaxVehicleMeshBytes)||uint64_t(chunk.offset)+chunk.byteCount>chunk.totalBytes)return false;
        std::vector<uint8_t> complete;
        {
            std::lock_guard lock(mutex_);auto& pending=partialVehicleMesh_;
            if(!chunk.offset){
                if(pending.epoch&&!(chunk.epoch==pending.epoch?newerSerial(chunk.frame,pending.frame):newerSerial(chunk.epoch,pending.epoch)))return false;
                pending.epoch=chunk.epoch;pending.frame=chunk.frame;pending.totalBytes=chunk.totalBytes;pending.received=0;
                pending.kind=type;
                pending.bytes.resize(chunk.totalBytes);
            }
            if(pending.epoch!=chunk.epoch||pending.frame!=chunk.frame||pending.totalBytes!=chunk.totalBytes
               ||pending.received!=chunk.offset||pending.bytes.size()!=chunk.totalBytes||pending.kind!=type)return false;
            std::memcpy(pending.bytes.data()+chunk.offset,payload.data()+at,chunk.byteCount);
            pending.received+=chunk.byteCount;
            if(pending.received==pending.totalBytes)complete=std::move(pending.bytes);
        }
        if(complete.empty())return true;
        Mesh mesh{};VehicleMeshCapture capture{};std::vector<MeshBatch> batches;
        if(!indexedVehicleMesh({complete.data(),complete.size()},chunk.epoch,chunk.frame,mesh,batches,surface,&capture))return false;
        std::lock_guard lock(mutex_);
        // A concurrent newer frame may already have superseded this one while
        // its source vertex palette was being validated outside the lock.
        if(partialVehicleMesh_.epoch==chunk.epoch&&partialVehicleMesh_.frame==chunk.frame){
            mesh_=std::move(batches);meshHeader_=mesh;newMesh_=true;meshTime_=std::chrono::steady_clock::now();
            surfaceMesh_=surface;meshCapture_=capture;
            // Reuse the one bounded assembly allocation for the next car
            // frame. Retained epoch/frame still rejects completed replays.
            partialVehicleMesh_.bytes=std::move(complete);partialVehicleMesh_.bytes.clear();
        }
        return true;
    }
    if(type==Message::Mesh){
        Mesh m{};if(!read(payload,at,m)||(m.category!=1&&m.category!=2&&m.category!=3)||m.batchCount>MaxBatches)return false;
        for(double value:{m.hostOriginX,m.hostOriginY,m.hostOriginZ})if(!std::isfinite(value)||std::abs(value)>30000000.)return false;
        std::vector<MeshBatch> batches;batches.reserve(m.batchCount);uint64_t total=0;
        for(uint32_t i=0;i<m.batchCount;i++){
            Batch b{};if(!read(payload,at,b)||b.vertexCount%3 || !validMeshMaterial(b.flags) || (total+=b.vertexCount)>MaxVertices)return false;
            size_t bytes=size_t(b.vertexCount)*sizeof(Vertex);if(at>payload.size()||bytes>payload.size()-at)return false;
            MeshBatch batch{b.textureId,b.flags,{}};batch.vertices.resize(b.vertexCount);
            if(bytes)std::memcpy(batch.vertices.data(),payload.data()+at,bytes);at+=bytes;
            for(const Vertex& v:batch.vertices)if(!validVertex(v))return false;
            batches.push_back(std::move(batch));
        }
        if(at!=payload.size())return false;
        std::lock_guard lock(mutex_);
        if(m.category==1){mesh_=std::move(batches);meshHeader_=m;newMesh_=true;meshTime_=std::chrono::steady_clock::now();surfaceMesh_=false;meshCapture_={};}
        else if(m.category==2){scene_=std::move(batches);sceneHeader_=m;newScene_=true;}
        else{entities_=std::move(batches);entityHeader_=m;newEntities_=true;}return true;
    }
    if(type==Message::Motion){
        GuestMotion motion{};if(payload.size()!=sizeof(motion)||!read(payload,at,motion)||!validMotion(motion)||(vehiclePeer_&&(motion.flags&MotionNativeTraversalAck)))return false;
        std::lock_guard lock(mutex_);motion_=motion;newMotion_=true;motionTime_=std::chrono::steady_clock::now();return true;
    }
    if(type==Message::VehicleInfo){
        VehicleInfo info{};
        if(!vehiclePeer_||payload.size()!=sizeof(info)||!read(payload,at,info)||!info.epoch||!info.vehicleId
           ||!info.nodeCount||info.nodeCount>20000||(info.flags&~1u)||info.reserved
           ||!std::isfinite(info.collisionRadius)||info.collisionRadius<=0||info.collisionRadius>128)return false;
        std::lock_guard lock(mutex_);vehicle_=info;vehicleTime_=std::chrono::steady_clock::now();return true;
    }
    if(type==Message::VehiclePose){
        VehiclePose pose{};
        if(!vehiclePeer_||payload.size()!=sizeof(pose)||!read(payload,at,pose)||!validVehiclePose(pose))return false;
        std::lock_guard lock(mutex_);
        if(vehiclePose_.epoch==pose.epoch&&vehiclePose_.vehicleId==pose.vehicleId
           &&(pose.sourceSeconds<vehiclePose_.sourceSeconds||pose.tick<=vehiclePose_.tick)){
            if(vehiclePoseIgnored_<std::numeric_limits<uint64_t>::max())++vehiclePoseIgnored_;return true;
        }
        if(vehiclePose_.epoch&&pose.epoch!=vehiclePose_.epoch&&!newerSerial(pose.epoch,vehiclePose_.epoch)){
            if(vehiclePoseIgnored_<std::numeric_limits<uint64_t>::max())++vehiclePoseIgnored_;return true;
        }
        if(vehiclePose_.epoch!=pose.epoch||vehiclePose_.vehicleId!=pose.vehicleId)vehiclePoses_.clear();
        vehiclePose_=pose;vehiclePoseTime_=std::chrono::steady_clock::now();
        if(vehiclePoseAccepted_<std::numeric_limits<uint64_t>::max())++vehiclePoseAccepted_;
        if(vehiclePoses_.size()>=128)vehiclePoses_.erase(vehiclePoses_.begin());
        vehiclePoses_.push_back({pose,vehiclePresentationNowMs()});return true;
    }
    if(type==Message::VehicleMaterial){
        VehicleMaterial h{};if(!vehiclePeer_||!read(payload,at,h)||h.layerCount<1||h.layerCount>4
            ||payload.size()-at!=h.layerCount*sizeof(VehicleMaterialLayer))return false;
        std::vector<VehicleMaterialLayer> layers(h.layerCount);
        for(auto& layer:layers)if(!read(payload,at,layer))return false;
        if(!validVehicleMaterial(h,layers))return false;
        std::lock_guard lock(mutex_);if(vehicleMaterials_.size()>=256&&!vehicleMaterials_.count(h.id))return false;
        vehicleMaterials_[h.id]={h,std::move(layers)};return true;
    }
    if(type==Message::VehicleDashboard){
        VehicleDashboard dashboard{};
        if(!vehiclePeer_||payload.size()!=sizeof(dashboard)||!read(payload,at,dashboard)||!validVehicleDashboard(dashboard))return false;
        std::lock_guard lock(mutex_);
        if(vehicleDashboard_.epoch==dashboard.epoch&&vehicleDashboard_.vehicleId==dashboard.vehicleId&&dashboard.sourceSeconds<vehicleDashboard_.sourceSeconds)return true;
        if(vehicleDashboard_.epoch&&dashboard.epoch!=vehicleDashboard_.epoch&&!newerSerial(dashboard.epoch,vehicleDashboard_.epoch))return true;
        vehicleDashboard_=dashboard;vehicleDashboardTime_=std::chrono::steady_clock::now();newVehicleDashboard_=true;return true;
    }
    if(type==Message::Melee){
        GuestMelee melee{};if(payload.size()!=sizeof(melee)||!read(payload,at,melee)||!validMelee(melee,vehiclePeer_))return false;
        std::lock_guard lock(mutex_);if(melee_.size()>=128)return false;melee_.push_back(melee);return true;
    }
    if(type==Message::Blocks){
        BlockColliders header{};if(!read(payload,at,header)||header.count>4096||payload.size()-at!=size_t(header.count)*sizeof(BlockBox))return false;
        std::vector<BlockBox> boxes(header.count);
        for(auto& box:boxes){if(!read(payload,at,box))return false;
            const float lower[]{box.minX,box.minY,box.minZ},upper[]{box.maxX,box.maxY,box.maxZ};
            for(int axis=0;axis<3;axis++)if(!std::isfinite(lower[axis])||!std::isfinite(upper[axis])||std::abs(lower[axis])>30000000||std::abs(upper[axis])>30000000||lower[axis]>=upper[axis]||upper[axis]-lower[axis]>64)return false;
        }
        std::lock_guard lock(mutex_);blockEpoch_=header.epoch;blocks_=std::move(boxes);newBlocks_=true;return true;
    }
    if(type==Message::Hud){
        HudData data{};if(!decodedHud(payload,data))return false;
        std::lock_guard lock(mutex_);hud_=std::move(data);newHud_=true;hudTime_=std::chrono::steady_clock::now();return true;
    }
    return type==Message::Goodbye && payload.empty();
}
void Transport::run(uint16_t port){
    WSADATA ws{};if(WSAStartup(MAKEWORD(2,2),&ws)!=0){note("Winsock initialization failed");return;}
    while(!stopping_){
        SOCKET sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(sock==INVALID_SOCKET)break;
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        DWORD timeout=250;setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        int yes=1;setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<const char*>(&yes),sizeof(yes));
        if(connect(sock,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0){
            closesocket(sock);for(int i=0;i<20&&!stopping_;i++)std::this_thread::sleep_for(std::chrono::milliseconds(50));continue;
        }
        bool allocationFailed=false;
        try {
        reset();connected_=true;note(vehiclePeer_?"Connected to BeamNG bridge":"Connected to Minecraft bridge");
        BeamngUiReader originalUi;
        uint32_t reportedUiOwner{};uint64_t reportedUiGeneration{};
        std::vector<uint8_t> stream;stream.reserve(64*1024);
        uint32_t sequence=0;auto lastSend=std::chrono::steady_clock::time_point{},lastUiPoll=lastSend;bool ok=true;
        while(!stopping_&&ok){
            auto now=std::chrono::steady_clock::now();
            if(vehiclePeer_&&now-lastUiPoll>=std::chrono::milliseconds(20)){
                Input current{};bool fresh{};uint64_t generation{};{std::lock_guard lock(mutex_);current=input_;generation=uiGeneration_;fresh=now-inputTime_<std::chrono::milliseconds(250);}
                UiCapture capture{};
                const bool captured=originalUi.poll(uintptr_t(sock),current.hostEpoch,fresh&&(current.flags&Enabled),GetTickCount64(),capture);
                if(originalUi.owner()&&originalUi.owner()!=reportedUiOwner){
                    reportedUiOwner=originalUi.owner();char line[160]{};std::snprintf(line,sizeof(line),"Original BeamNG UI owner verified from connected socket: pid=%u.",reportedUiOwner);note(line);
                }
                if(captured){
                    HudData data{};
                    if(decodedHud({capture.payload.data(),capture.payload.size()},data)){
                        data.uiModeKnown=true;data.uiScreenOpen=capture.screenOpen;
                        const auto capturedNow=GetTickCount64();const auto publicationNow=std::chrono::steady_clock::now();
                        bool published=false;const auto width=data.width,height=data.height;
                        {std::lock_guard lock(mutex_);
                            if(connected_&&generation==uiGeneration_&&current.hostEpoch==input_.hostEpoch&&(input_.flags&Enabled)
                               &&publicationNow-inputTime_<std::chrono::milliseconds(250)&&capturedNow>=capture.stamp
                               &&capturedNow-capture.stamp<=UiMappingMaxAge){
                                hud_=std::move(data);newHud_=true;
                                hudTime_=publicationNow-std::chrono::milliseconds(capturedNow-capture.stamp);published=true;
                            }
                        }
                        if(published&&generation!=reportedUiGeneration){reportedUiGeneration=generation;char line[240]{};
                            std::snprintf(line,sizeof(line),"Original BeamNG UI first fresh frame: epoch=%u sourceSequence=%llu dimensions=%ux%u captureAge=%llu ms encodedBytes=%zu.",current.hostEpoch,static_cast<unsigned long long>(capture.sequence),width,height,static_cast<unsigned long long>(capturedNow-capture.stamp),capture.payload.size());note(line);}
                    }
                }
                lastUiPoll=std::chrono::steady_clock::now();
            }
            if(now-lastSend>=std::chrono::microseconds(16667)){
                Input input{};{std::lock_guard lock(mutex_);input=input_;input_.pressed=0;input_.scroll=0;
                    if(now-inputTime_>std::chrono::milliseconds(250)){input.flags=0;input.held=0;input.pressed=0;}}
                // The vehicle peer needs an actual host epoch before creating
                // its anchor. Socket startup can precede the first host frame.
                if(!vehiclePeer_||input.hostEpoch){
                    Header h{Magic,ProtocolVersion,Message::Input,sizeof(Input),++sequence};
                    ok=sendAll(sock,reinterpret_cast<const uint8_t*>(&h),sizeof(h))&&sendAll(sock,reinterpret_cast<const uint8_t*>(&input),sizeof(input));
                }
                lastSend=now;
            }
            std::deque<Outgoing> outgoing;{std::lock_guard lock(mutex_);outgoing.swap(outgoing_);}
            for(const auto& packet:outgoing){
                Header h{Magic,ProtocolVersion,packet.type,static_cast<uint32_t>(packet.payload.size()),++sequence};
                if(!sendAll(sock,reinterpret_cast<const uint8_t*>(&h),sizeof(h))||!sendAll(sock,packet.payload.data(),packet.payload.size())){ok=false;break;}
            }
            if(!ok)break;
            fd_set readers;FD_ZERO(&readers);FD_SET(sock,&readers);timeval wait{0,2000};
            int ready=select(0,&readers,nullptr,nullptr,&wait);if(ready<0)break;if(ready==0)continue;
            uint8_t block[65536];int count=recv(sock,reinterpret_cast<char*>(block),sizeof(block),0);if(count<=0)break;
            stream.insert(stream.end(),block,block+count);
            size_t consumed=0;
            while(stream.size()-consumed>=sizeof(Header)){
                Header h{};std::memcpy(&h,stream.data()+consumed,sizeof(h));
                if(h.magic!=Magic||h.version!=ProtocolVersion||h.size>MaxPayload){ok=false;break;}
                if(stream.size()-consumed<sizeof(h)+h.size)break;
                if(!acceptBytes(h.type,stream.data()+consumed+sizeof(h),h.size)){note(rejectedPacket(h.type,h.sequence,{stream.data()+consumed+sizeof(h),h.size},vehiclePeer_).c_str());ok=false;break;}
                consumed+=sizeof(h)+h.size;
                if(h.type==Message::Goodbye){ok=false;break;}
            }
            if(consumed)stream.erase(stream.begin(),stream.begin()+consumed);
            if(stream.size()>MaxPayload+sizeof(Header))ok=false;
        }
        }catch(const std::bad_alloc&){
            // Any connected receive/decode/UI allocation can fail in the
            // 32-bit host. Unwind all local buffers before clearing authority
            // and closing this exact socket; never continue a half image.
            allocationFailed=true;
        }
        shutdown(sock,SD_BOTH);closesocket(sock);reset(allocationFailed);
        if(allocationFailed){
            if(allocationFailures_<4){++allocationFailures_;note(vehiclePeer_?
                "BeamNG bridge allocation failed; connection and assets released, native control restored. Retrying after 1 second.":
                "Minecraft bridge allocation failed; connection and assets released, native control restored. Retrying after 1 second.");}
            // Let EndScene consume the disconnected/new-session mailbox and
            // retire its renderer resources before another asset download.
            for(int i=0;i<20&&!stopping_;++i)std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }else note(vehiclePeer_?"BeamNG bridge disconnected; restoring host control":"Minecraft bridge disconnected; restoring host control");
    }
    WSACleanup();
}
}
