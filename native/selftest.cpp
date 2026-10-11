#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include "transport.hpp"
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <new>
#include "input_mapping.hpp"
#include "texture_queue_budget.hpp"
#include "vehicle_texture_storage.hpp"
#include <future>

namespace re4craft {
// No public runtime switch grants this ability. The fixture exercises the
// production socket worker and decoder, with a fault at its exact allocation.
struct TransportAllocationTestAccess {
    static void inject(Transport& t,void (*check)(size_t)){t.textureAllocationCheck_=check;}
    static void compressHook(Transport& t,void (*check)()){t.textureCompressionCheck_=check;}
    static void resetSession(Transport& t){t.reset();}
    static void hudMode(Transport& t,bool menu){
        std::lock_guard lock(t.mutex_);t.connected_=true;t.hud_={1,1,0,1,{1,2,3,4},true,menu};
        t.hudTime_=std::chrono::steady_clock::now();t.newHud_=true;
    }
    static bool primed(Transport& t){
        std::lock_guard lock(t.mutex_);
        return t.connected_&&t.stateTime_.time_since_epoch().count()&&t.motionTime_.time_since_epoch().count()
            &&t.vehicleTime_.time_since_epoch().count()&&t.hudTime_.time_since_epoch().count()
            &&t.textures_.size()==1&&t.partialTextures_.size()==1;
    }
    static bool cleared(Transport& t){
        std::lock_guard lock(t.mutex_);
        return !t.connected_&&!t.stateTime_.time_since_epoch().count()&&!t.meshTime_.time_since_epoch().count()
            &&!t.motionTime_.time_since_epoch().count()&&!t.hudTime_.time_since_epoch().count()
            &&!t.vehicleTime_.time_since_epoch().count()&&!t.vehiclePoseTime_.time_since_epoch().count()
            &&!t.vehicleDashboardTime_.time_since_epoch().count()&&t.textures_.empty()&&t.partialTextures_.empty()
            &&t.partialVehicleMesh_.bytes.empty()&&!t.partialVehicleMesh_.bytes.capacity()
            &&t.mesh_.empty()&&!t.mesh_.capacity()&&t.scene_.empty()&&!t.scene_.capacity()
            &&t.entities_.empty()&&!t.entities_.capacity()&&t.vehicleMaterials_.empty()&&t.vehiclePoses_.empty()
            &&!t.vehiclePoses_.capacity()&&t.hud_.rgba.empty()&&!t.hud_.rgba.capacity()
            &&t.blocks_.empty()&&!t.blocks_.capacity()&&t.melee_.empty()&&!t.melee_.capacity()
            &&t.outgoing_.empty()&&!t.input_.flags&&!t.input_.held&&!t.input_.pressed&&!t.input_.scroll
            &&!t.input_.mouseButtons&&!t.inputTime_.time_since_epoch().count()&&t.newSession_;
    }
};
}

namespace {
template<class T> std::vector<uint8_t> bytes(const T& value){
    std::vector<uint8_t> b(sizeof(T));std::memcpy(b.data(),&value,sizeof(T));return b;
}
struct FixtureSocket {
    SOCKET value{INVALID_SOCKET};
    ~FixtureSocket(){if(value!=INVALID_SOCKET)closesocket(value);}
};
std::atomic<unsigned> allocationFaultCalls{},allocationFailureNotes{};
std::atomic<size_t> failedTextureBytes{};
std::atomic<bool> allocationFaultArmed{};
std::atomic<bool> compressionWaitArmed{},compressionEntered{},compressionRelease{};
void pauseCompression(){
    if(!compressionWaitArmed.exchange(false))return;
    compressionEntered=true;const auto deadline=GetTickCount64()+2000;
    while(!compressionRelease&&GetTickCount64()<deadline)Sleep(1);
}
int compactTextureFixture(){
    using namespace re4craft;
    // Exercise the production compressed receiver, every original channel
    // format, exact reconstruction and publication only after a full image.
    Transport peer({},true,true);
    for(uint32_t format:{1u,2u,4u,260u}){
        const uint32_t channels=format&255u,id=5000+format;
        std::vector<uint8_t> raw(256u*256u*channels);
        for(size_t i=0;i<raw.size();++i)raw[i]=uint8_t((i%channels)*47+31);
        for(uint32_t offset=0;offset<raw.size();){
            const auto count=uint32_t(std::min<size_t>(65536,raw.size()-offset));
            auto p=bytes(VehicleTextureChunk{id,256,256,offset,count,format,0});
            p.insert(p.end(),raw.begin()+offset,raw.begin()+offset+count);
            if(!peer.accept(Message::VehicleTextureChunk,p))return 146;offset+=count;
            if(offset<raw.size()&&!peer.takeUpdates().textures.empty())return 147;
        }
        auto u=peer.takeUpdates();if(u.textures.size()!=1)return 148;
        const auto& image=u.textures[0];std::vector<uint8_t> decoded;
        if(image.id!=id||image.format!=format||!image.compressed||image.rgba.size()>=raw.size()
           ||!decodedTexture(image.rgba,raw.size(),decoded)||decoded!=raw)return 149;
    }
    // Pause on the actual receiving path after the mailbox unlock. Draining
    // must stay responsive; reset or a newer revision cancels old publication.
    for(bool legacy:{false,true})for(bool reset:{false,true}){
        Transport raced({},true,true);TransportAllocationTestAccess::compressHook(raced,pauseCompression);
        compressionWaitArmed=true;compressionEntered=false;compressionRelease=false;
        std::vector<uint8_t> raw(64u*64u*4u,17);
        auto p=legacy?bytes(Texture{77,64,64}):bytes(VehicleTextureChunk{77,64,64,0,uint32_t(raw.size()),4,0});
        p.insert(p.end(),raw.begin(),raw.end());
        const auto kind=legacy?Message::Texture:Message::VehicleTextureChunk;
        auto receiver=std::async(std::launch::async,[&]{return raced.accept(kind,p);});
        auto deadline=GetTickCount64()+2000;while(!compressionEntered&&GetTickCount64()<deadline)Sleep(1);
        if(!compressionEntered){compressionRelease=true;receiver.wait();return 150;}
        auto drain=std::async(std::launch::async,[&]{return raced.takeUpdates();});
        const bool responsive=drain.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
        if(!responsive){compressionRelease=true;receiver.wait();drain.wait();return 151;}
        if(!drain.get().textures.empty()){compressionRelease=true;receiver.wait();return 152;}
        if(reset)TransportAllocationTestAccess::resetSession(raced);
        else {
            const auto tiny=bytes(Texture{77,1,1});auto latest=tiny;latest.insert(latest.end(),{31,47,63,255});
            if(!raced.accept(Message::Texture,latest)){compressionRelease=true;receiver.wait();return 153;}
        }
        compressionRelease=true;if(!receiver.get())return 154;
        auto u=raced.takeUpdates();
        if(reset){if(!u.textures.empty())return 155;}
        else if(u.textures.size()!=1||u.textures[0].width!=1||u.textures[0].compressed
                ||u.textures[0].rgba!=std::vector<uint8_t>({31,47,63,255}))return 156;
    }
    Transport ui({},true,true);TransportAllocationTestAccess::hudMode(ui,true);
    auto first=ui.takeUpdates(),next=ui.takeUpdates();
    if(!first.newHud||!first.hud.uiModeKnown||!first.hud.uiScreenOpen||!first.hudUiModeKnown||!first.hudUiScreenOpen
       ||next.newHud||!next.hudValid||!next.hudUiModeKnown||!next.hudUiScreenOpen)return 157;
    TransportAllocationTestAccess::resetSession(ui);next=ui.takeUpdates();
    if(next.hudValid||next.hudUiModeKnown||next.hudUiScreenOpen)return 158;
    // Catalog IDs vary on replacement. Drained historical revisions cannot
    // consume the bounded in-flight serial ledger across later selections.
    for(uint32_t id=6000;id<6300;++id){
        auto p=bytes(VehicleTextureChunk{id,1,1,0,1,1,0});p.push_back(47);
        if(!peer.accept(Message::VehicleTextureChunk,p)||peer.takeUpdates().textures.size()!=1)return 159;
    }
    return 0;
}
void textureAllocationFault(size_t count){
    if(count==64u*1024u*1024u&&allocationFaultArmed.exchange(false)){
        failedTextureBytes=count;++allocationFaultCalls;throw std::bad_alloc();
    }
}
bool fixtureWrite(SOCKET s,const void* source,size_t bytes){
    auto* p=static_cast<const char*>(source);
    while(bytes){const int n=send(s,p,static_cast<int>(bytes),0);if(n<=0)return false;p+=n;bytes-=n;}
    return true;
}
bool fixtureRead(SOCKET s,void* target,size_t bytes){
    auto* p=static_cast<char*>(target);
    while(bytes){const int n=recv(s,p,static_cast<int>(bytes),0);if(n<=0)return false;p+=n;bytes-=n;}
    return true;
}
bool fixturePacket(SOCKET s,re4craft::Message kind,const std::vector<uint8_t>& payload){
    const re4craft::Header h{re4craft::Magic,re4craft::ProtocolVersion,kind,static_cast<uint32_t>(payload.size()),1};
    return fixtureWrite(s,&h,sizeof(h))&&fixtureWrite(s,payload.data(),payload.size());
}
SOCKET fixtureAccept(SOCKET listener){
    fd_set readers;FD_ZERO(&readers);FD_SET(listener,&readers);timeval timeout{3,0};
    if(select(0,&readers,nullptr,nullptr,&timeout)!=1)return INVALID_SOCKET;
    SOCKET client=accept(listener,nullptr,nullptr);
    if(client!=INVALID_SOCKET){DWORD wait=1500;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&wait),sizeof(wait));}
    return client;
}
int allocationRecoveryFixture(){
    using namespace re4craft;
    WSADATA ws{};if(WSAStartup(MAKEWORD(2,2),&ws))return 130;
    struct WinsockCleanup {~WinsockCleanup(){WSACleanup();}} cleanup;
    FixtureSocket listener;listener.value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener.value==INVALID_SOCKET)return 131;
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(listener.value,reinterpret_cast<const sockaddr*>(&addr),sizeof(addr))||listen(listener.value,2))return 132;
    int length=sizeof(addr);if(getsockname(listener.value,reinterpret_cast<sockaddr*>(&addr),&length))return 133;
    allocationFaultCalls=0;allocationFailureNotes=0;failedTextureBytes=0;allocationFaultArmed=true;
    Transport peer([](const char* line){if(std::strstr(line,"allocation failed"))++allocationFailureNotes;},true);
    TransportAllocationTestAccess::inject(peer,textureAllocationFault);
    Input input{};input.flags=Enabled|Focused|GuestAuthority;input.hostEpoch=7;input.held=Forward;input.pressed=Inventory;input.scroll=120;input.mouseButtons=1;input.selectedSlot=UINT32_MAX;
    peer.setInput(input);peer.start(ntohs(addr.sin_port));
    FixtureSocket first;first.value=fixtureAccept(listener.value);if(first.value==INVALID_SOCKET)return 134;
    auto compact=[](VehicleTextureChunk prefix,std::initializer_list<uint8_t> pixels){auto p=bytes(prefix);p.insert(p.end(),pixels);return p;};
    State state{};state.flags=InWorld;state.health=state.maxHealth=20;state.food=20;state.tick=123;
    GuestMotion motion{};motion.epoch=7;motion.flags=MotionReady|MotionCameraReady;motion.health=motion.maxHealth=20;motion.fov=70;
    Hud hud{1,1,0,1};auto image=bytes(hud);image.insert(image.end(),{1,2,3,4});
    if(!fixturePacket(first.value,Message::State,bytes(state))||!fixturePacket(first.value,Message::Motion,bytes(motion))
        ||!fixturePacket(first.value,Message::VehicleInfo,bytes(VehicleInfo{7,42,757,1,2,0}))
        ||!fixturePacket(first.value,Message::Hud,image)
        ||!fixturePacket(first.value,Message::VehicleTextureChunk,compact({11,1,1,0,4,260,0},{1,2,3,4}))
        ||!fixturePacket(first.value,Message::VehicleTextureChunk,compact({12,2,1,0,1,1,0},{17})))return 135;
    auto deadline=GetTickCount64()+2000;
    while(!TransportAllocationTestAccess::primed(peer)&&GetTickCount64()<deadline)Sleep(5);
    if(!TransportAllocationTestAccess::primed(peer))return 136;
    if(!fixturePacket(first.value,Message::VehicleTextureChunk,compact({1006,4096,4096,0,1,260,0},{31})))return 137;
    deadline=GetTickCount64()+2000;
    while(!TransportAllocationTestAccess::cleared(peer)&&GetTickCount64()<deadline)Sleep(5);
    if(allocationFaultCalls!=1||failedTextureBytes!=64u*1024u*1024u||!TransportAllocationTestAccess::cleared(peer))return 138;
    char pending[512];int received;
    do{received=recv(first.value,pending,sizeof(pending),0);}while(received>0);
    if(received!=0)return 139; // The exact failed connection reached EOF.
    auto dropped=peer.takeUpdates();
    if(dropped.connected||dropped.stateValid||dropped.meshValid||dropped.hudValid||dropped.motionValid
        ||dropped.vehicleValid||dropped.vehiclePoseValid||dropped.vehicleDashboardValid||!dropped.newSession
        ||dropped.newMesh||dropped.newScene||dropped.newEntities||dropped.newHud||dropped.newMotion
        ||!dropped.textures.empty()||!dropped.mesh.empty()||!dropped.vehicleMaterials.empty()||!dropped.melee.empty())return 140;
    // The failed session's unfinished image is gone. Its tail cannot revive
    // it, and the same production schema/range checks still reject bad input.
    if(peer.accept(Message::VehicleTextureChunk,compact({12,2,1,1,1,1,0},{55}))
        ||peer.accept(Message::VehicleTextureChunk,compact({11,4097,1,0,1,1,0},{1}))
        ||peer.accept(Message::VehicleTextureChunk,compact({11,1,1,0,1,257,0},{1}))
        ||peer.accept(Message::VehicleTextureChunk,compact({11,1,1,0,1,1,1},{1})))return 141;
    FixtureSocket second;second.value=fixtureAccept(listener.value);if(second.value==INVALID_SOCKET)return 142;
    Header releaseHeader{};Input released{};
    if(!fixtureRead(second.value,&releaseHeader,sizeof(releaseHeader))||releaseHeader.magic!=Magic
        ||releaseHeader.version!=ProtocolVersion||releaseHeader.type!=Message::Input||releaseHeader.size!=sizeof(Input)
        ||!fixtureRead(second.value,&released,sizeof(released))||released.flags||released.held||released.pressed
        ||released.scroll||released.mouseButtons||released.hostEpoch!=7)return 143;
    input.hostEpoch=8;input.pressed=0;input.scroll=0;input.mouseButtons=0;peer.setInput(input);
    motion.epoch=8;state.tick=456;
    if(!fixturePacket(second.value,Message::State,bytes(state))||!fixturePacket(second.value,Message::Motion,bytes(motion))
        ||!fixturePacket(second.value,Message::VehicleInfo,bytes(VehicleInfo{8,43,757,1,2,0}))
        ||!fixturePacket(second.value,Message::Mesh,bytes(Mesh{1,3,0,8,1,2,3}))
        ||!fixturePacket(second.value,Message::VehicleTextureChunk,compact({11,2,1,0,4,260,0},{31,47,63,255}))
        ||!fixturePacket(second.value,Message::VehicleTextureChunk,compact({11,2,1,4,4,260,0},{17,55,91,255})))return 144;
    bool readyState=false,readyMotion=false,readyVehicle=false,readyMesh=false,readyPixels=false;
    deadline=GetTickCount64()+2000;
    while(GetTickCount64()<deadline){
        auto u=peer.takeUpdates();readyState|=u.stateValid&&u.state.tick==456;
        readyMotion|=u.motionValid&&u.motion.epoch==8;readyVehicle|=u.vehicleValid&&u.vehicle.vehicleId==43;
        readyMesh|=u.newMesh&&u.meshHeader.epoch==8;
        for(const auto& texture:u.textures)readyPixels|=texture.id==11&&texture.width==2&&texture.height==1&&texture.format==260
            &&texture.rgba==std::vector<uint8_t>({31,47,63,255,17,55,91,255});
        if(readyState&&readyMotion&&readyVehicle&&readyMesh&&readyPixels)break;Sleep(5);
    }
    if(!readyState||!readyMotion||!readyVehicle||!readyMesh||!readyPixels||allocationFailureNotes!=1)return 145;
    peer.stop();return 0;
}
}
extern "C" __declspec(dllexport) int __cdecl RE4CraftProtocolSelfTest(){
    using namespace re4craft;
    Transport t;
    State s{};s.health=20;s.maxHealth=20;s.food=20;s.flags=InWorld;s.tick=12345;
    if(!t.accept(Message::State,bytes(s)))return 1;
    auto truncated=bytes(s);truncated.pop_back();if(t.accept(Message::State,truncated))return 2;
    s.yaw=NAN;if(t.accept(Message::State,bytes(s)))return 3;
    Texture bad{7,0xffffffff,2};if(t.accept(Message::Texture,bytes(bad)))return 4;
    Mesh over{1,42,MaxBatches+1};if(t.accept(Message::Mesh,bytes(over)))return 5;
    Mesh m{1,42,1};Batch b{7,0,3};Vertex v[3]{{0,0,0,0,1,0,0,0,0xffffffff},{1,0,0,0,1,0,1,0,0xffffffff},{0,1,0,0,1,0,0,1,0xffffffff}};
    auto packet=bytes(m);auto bb=bytes(b);packet.insert(packet.end(),bb.begin(),bb.end());
    auto start=reinterpret_cast<const uint8_t*>(v);packet.insert(packet.end(),start,start+sizeof(v));
    if(!t.accept(Message::Mesh,packet))return 6;
    packet.push_back(0);if(t.accept(Message::Mesh,packet))return 7;
    packet.pop_back();Mesh scene{2,43,1};std::memcpy(packet.data(),&scene,sizeof(Mesh));
    if(!t.accept(Message::Mesh,packet)||!t.takeUpdates().newScene)return 8;
    Hud hud{2,2,1,44};auto image=bytes(hud);image.insert(image.end(),16,255);
    if(!t.accept(Message::Hud,image)||!t.takeUpdates().newHud)return 9;
    image.pop_back();if(t.accept(Message::Hud,image))return 10;
    Hud compressed{2,2,3,45};auto rle=bytes(compressed);auto run=bytes(uint32_t(0x80000004)),color=bytes(uint32_t(0xabcdef12));
    rle.insert(rle.end(),run.begin(),run.end());rle.insert(rle.end(),color.begin(),color.end());
    if(!t.accept(Message::Hud,rle))return 11;auto update=t.takeUpdates();
    if(update.hud.flags!=1||update.hud.rgba.size()!=16||std::memcmp(update.hud.rgba.data(),color.data(),4))return 12;
    uint32_t overflow=0x80000005;std::memcpy(rle.data()+sizeof(Hud),&overflow,4);if(t.accept(Message::Hud,rle))return 13;
    overflow=0x80000000;std::memcpy(rle.data()+sizeof(Hud),&overflow,4);if(t.accept(Message::Hud,rle))return 14;
    GuestMotion motion{};motion.flags=MotionReady;motion.health=motion.maxHealth=20;motion.fov=70;
    if(!t.accept(Message::Motion,bytes(motion))||!t.takeUpdates().newMotion)return 15;
    motion.cameraX=NAN;if(t.accept(Message::Motion,bytes(motion)))return 16;
    // A landing receipt is distinct from movement authority. Exercise the
    // production decoder/mailbox, including malformed and foreign-peer ACKs.
    {
        Transport landingPeer,vehicleLandingPeer({},true);
        GuestMotion receipt{};receipt.epoch=9;receipt.tick=100;
        receipt.flags=MotionNativeTraversalAck|MotionCameraReady;
        receipt.health=receipt.maxHealth=20;receipt.fov=70;
        receipt.hostX=3;receipt.hostY=-2;receipt.hostZ=5;
        if(!landingPeer.accept(Message::Motion,bytes(receipt)))return 180;
        auto landing=landingPeer.takeUpdates();
        if(!landing.newMotion||landing.motion.tick!=100
           ||landing.motion.flags!=receipt.flags||landing.motion.hostY!=-2)return 181;
        for(uint32_t invalidFlag:{uint32_t(MotionReady),uint32_t(MotionDead),uint32_t(MotionScreen),64u}){
            auto invalid=receipt;invalid.flags|=invalidFlag;invalid.tick=101;
            if(landingPeer.accept(Message::Motion,bytes(invalid)))return 182;
        }
        auto invalid=receipt;invalid.flags&=~MotionCameraReady;
        if(landingPeer.accept(Message::Motion,bytes(invalid)))return 183;
        invalid=receipt;invalid.health=0;
        if(landingPeer.accept(Message::Motion,bytes(invalid)))return 184;
        if(vehicleLandingPeer.accept(Message::Motion,bytes(receipt)))return 185;
        landing=landingPeer.takeUpdates();
        if(landing.newMotion||landing.motion.tick!=100)return 186;
        auto ready=receipt;ready.flags=MotionReady|MotionCameraReady;ready.tick=102;
        if(!landingPeer.accept(Message::Motion,bytes(ready)))return 187;
        landing=landingPeer.takeUpdates();
        if(!landing.newMotion||landing.motion.tick!=102||(landing.motion.flags&MotionNativeTraversalAck))return 188;
    }
    if(sdlPhysicalScancode(0x11)!=26||sdlPhysicalScancode(0x1e)!=4||sdlPhysicalScancode(0x1d)!=224||sdlPhysicalScancode(0x2a)!=225||sdlPhysicalScancode(0x1d,true)!=228||sdlPhysicalScancode(0x01)!=41)return 17;
    if(moveCursor(.5f,160,320)!=1||moveCursor(.5f,-160,320)!=0)return 18;
    BlockColliders blocks{3,1,55};BlockBox box{0,0,0,1,1,1};auto collider=bytes(blocks);auto bbx=bytes(box);collider.insert(collider.end(),bbx.begin(),bbx.end());
    if(!t.accept(Message::Blocks,collider))return 19;update=t.takeUpdates();if(!update.newBlocks||update.blockEpoch!=3||update.blocks.size()!=1)return 20;
    box.maxX=-1;std::memcpy(collider.data()+sizeof(blocks),&box,sizeof(box));if(t.accept(Message::Blocks,collider))return 21;
    // Entity updates have their own mailbox and must not overwrite the player's pose.
    Mesh entity{3,46,0,3,10,20,30};if(!t.accept(Message::Mesh,bytes(entity)))return 22;
    update=t.takeUpdates();if(!update.newEntities||update.newMesh||update.newScene||update.entityHeader.hostOriginY!=20||!update.entities.empty())return 23;
    GuestMelee hit{};hit.targetId=hit.eventId=1;hit.damage=4;hit.maxReach=1;hit.flags=HitHazard|HitFire;
    if(!t.accept(Message::Melee,bytes(hit)))return 24;
    hit.flags=HitExplosion;hit.maxReach=8;if(!t.accept(Message::Melee,bytes(hit)))return 25;
    hit.maxReach=17;if(t.accept(Message::Melee,bytes(hit)))return 26;
    hit.maxReach=1;hit.flags=HitHazard|HitExplosion;if(t.accept(Message::Melee,bytes(hit)))return 27;
    hit.flags=HitHazard|HitProjectile;if(t.accept(Message::Melee,bytes(hit)))return 28;
    hit.flags=64;if(t.accept(Message::Melee,bytes(hit)))return 29;
    hit.flags=HitProjectile;hit.maxReach=1.01f;if(t.accept(Message::Melee,bytes(hit)))return 30;
    hit.flags=0;hit.maxReach=6.01f;if(t.accept(Message::Melee,bytes(hit)))return 31;
    hit.flags=HitAlly;hit.maxReach=3;if(!t.accept(Message::Melee,bytes(hit)))return 40;
    hit.maxReach=3.01f;if(t.accept(Message::Melee,bytes(hit)))return 41;
    hit.maxReach=1;hit.flags=HitAlly|HitProjectile;if(t.accept(Message::Melee,bytes(hit)))return 42;
    auto materialPacket=bytes(Mesh{3,47,1});auto materialPrefix=bytes(Batch{7,0,3});
    materialPacket.insert(materialPacket.end(),materialPrefix.begin(),materialPrefix.end());
    materialPacket.insert(materialPacket.end(),start,start+sizeof(v));
    for(uint32_t flags:{0U,1U,2U,4U,8U,10U,16U,18U}){
        std::memcpy(materialPacket.data()+sizeof(Mesh)+sizeof(uint32_t),&flags,sizeof(flags));
        if(!t.accept(Message::Mesh,materialPacket))return 32;
    }
    for(uint32_t flags:{6U,12U,20U,32U}){
        std::memcpy(materialPacket.data()+sizeof(Mesh)+sizeof(uint32_t),&flags,sizeof(flags));
        if(t.accept(Message::Mesh,materialPacket))return 33;
    }
    // The dedicated vehicle decoder cannot grant Minecraft peers new flags or
    // publish half of a lossless texture. Offset and dimension changes reject.
    Transport vehicle({},true);
    auto chunkPacket=[&](TextureChunk prefix,std::initializer_list<uint8_t> content){
        auto data=bytes(prefix);data.insert(data.end(),content);return data;
    };
    const auto first=chunkPacket({11,2,1,0,4,0},{1,2,3,4});
    const auto last=chunkPacket({11,2,1,4,4,0},{5,6,7,8});
    if(t.accept(Message::TextureChunk,first)||!vehicle.accept(Message::TextureChunk,first))return 43;
    if(!vehicle.takeUpdates().textures.empty())return 44;
    if(vehicle.accept(Message::TextureChunk,first))return 45;
    if(vehicle.accept(Message::TextureChunk,chunkPacket({11,2,2,4,4,0},{5,6,7,8})))return 46;
    if(!vehicle.accept(Message::TextureChunk,last))return 47;
    auto completed=vehicle.takeUpdates();
    if(completed.textures.size()!=1||completed.textures[0].rgba!=std::vector<uint8_t>({1,2,3,4,5,6,7,8}))return 48;
    if(vehicle.accept(Message::TextureChunk,chunkPacket({12,4097,1,0,4,0},{1,2,3,4}))
       ||vehicle.accept(Message::TextureChunk,chunkPacket({12,1,1,0xffffffff,4,0},{1,2,3,4}))
       ||vehicle.accept(Message::TextureChunk,chunkPacket({12,1,1,0,4,1},{1,2,3,4})))return 49;
    VehicleInfo car{3,42,800,1,3.2f,0};
    if(t.accept(Message::VehicleInfo,bytes(car))||!vehicle.accept(Message::VehicleInfo,bytes(car))
       ||vehicle.takeUpdates().vehicle.vehicleId!=42)return 50;
    car.collisionRadius=NAN;if(vehicle.accept(Message::VehicleInfo,bytes(car)))return 51;
    hit.flags=HitVehicle;hit.maxReach=.3f;hit.damage=6;hit.knockback=.8f;
    if(t.accept(Message::Melee,bytes(hit))||!vehicle.accept(Message::Melee,bytes(hit)))return 52;
    hit.flags=HitVehicle|HitProjectile;if(vehicle.accept(Message::Melee,bytes(hit)))return 53;
    hit.flags=0;if(vehicle.accept(Message::Melee,bytes(hit)))return 54;
    // A real indexed frame is losslessly expanded only after all fragments.
    // Tiny fragments cross the Mesh header; unrelated fresh motion interleaves.
    auto indexed=bytes(Mesh{1,7,1,3,4,5,6});
    const auto indexedBatch=bytes(IndexedBatch{11,BatchDoubleSided,3,6});
    indexed.insert(indexed.end(),indexedBatch.begin(),indexedBatch.end());
    indexed.insert(indexed.end(),start,start+sizeof(v));
    for(uint32_t index:{2U,0U,1U,2U,1U,0U}){auto value=bytes(index);indexed.insert(indexed.end(),value.begin(),value.end());}
    auto vehicleFragment=[&](const std::vector<uint8_t>& body,uint32_t fragmentEpoch,uint32_t fragmentFrame,uint32_t offset,uint32_t count){
        auto data=bytes(VehicleMeshChunk{fragmentEpoch,fragmentFrame,static_cast<uint32_t>(body.size()),offset,count,0});
        data.insert(data.end(),body.begin()+offset,body.begin()+offset+count);return data;
    };
    auto partial=vehicleFragment(indexed,3,7,0,31);
    if(t.accept(Message::VehicleMeshChunk,partial)||!vehicle.accept(Message::VehicleMeshChunk,partial)
       ||vehicle.takeUpdates().newMesh)return 55;
    motion.cameraX=0;motion.epoch=3;motion.tick=99;
    if(!vehicle.accept(Message::Motion,bytes(motion)))return 56;
    update=vehicle.takeUpdates();if(!update.newMotion||update.motion.tick!=99||update.newMesh)return 57;
    if(vehicle.accept(Message::VehicleMeshChunk,partial)
       ||vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(indexed,3,7,32,1)))return 58;
    if(!vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(indexed,3,7,31,static_cast<uint32_t>(indexed.size())-31)))return 59;
    update=vehicle.takeUpdates();
    if(!update.newMesh||update.mesh.size()!=1||update.meshHeader.epoch!=3||update.meshHeader.frame!=7
       ||update.meshHeader.hostOriginY!=5||update.mesh[0].textureId!=11||update.mesh[0].flags!=BatchDoubleSided
       ||update.mesh[0].vertices.size()!=6)return 60;
    const uint32_t expectedIndices[]{2,0,1,2,1,0};
    for(size_t vertex=0;vertex<6;vertex++)if(std::memcmp(&update.mesh[0].vertices[vertex],&v[expectedIndices[vertex]],sizeof(Vertex)))return 61;
    if(vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(indexed,3,7,0,static_cast<uint32_t>(indexed.size()))))return 62;
    auto newer=indexed;uint32_t newFrame=8;std::memcpy(newer.data()+4,&newFrame,4);
    if(!vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(newer,3,8,0,31)))return 63;
    newFrame=9;std::memcpy(newer.data()+4,&newFrame,4);
    if(!vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(newer,3,9,0,31))
       ||vehicle.takeUpdates().newMesh)return 64;
    if(vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(indexed,3,8,31,static_cast<uint32_t>(indexed.size())-31)))return 65;
    if(!vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(newer,3,9,31,static_cast<uint32_t>(newer.size())-31))
       ||vehicle.takeUpdates().meshHeader.frame!=9)return 66;
    // Epoch change may restart the frame number; an older epoch cannot replace
    // either the pending frame or the retained visible car.
    newFrame=1;uint32_t newEpoch=4;std::memcpy(newer.data()+4,&newFrame,4);std::memcpy(newer.data()+12,&newEpoch,4);
    if(!vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(newer,4,1,0,static_cast<uint32_t>(newer.size())))
       ||vehicle.takeUpdates().meshHeader.epoch!=4
       ||vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(indexed,3,99,0,31)))return 67;
    auto invalidCarBody=[&](std::vector<uint8_t> body){
        Transport peer({},true);return peer.accept(Message::VehicleMeshChunk,vehicleFragment(body,3,7,0,static_cast<uint32_t>(body.size())));
    };
    auto corrupt=indexed;corrupt.push_back(0);if(invalidCarBody(corrupt))return 68;
    corrupt=indexed;newEpoch=4;std::memcpy(corrupt.data()+12,&newEpoch,4);if(invalidCarBody(corrupt))return 69;
    corrupt=indexed;newFrame=8;std::memcpy(corrupt.data()+4,&newFrame,4);if(invalidCarBody(corrupt))return 70;
    corrupt=indexed;uint32_t field=2;std::memcpy(corrupt.data(),&field,4);if(invalidCarBody(corrupt))return 71;
    for(const IndexedBatch invalid: {IndexedBatch{11,0,0,6},IndexedBatch{11,0,3,4},IndexedBatch{11,0,7,6},
        IndexedBatch{11,32,3,6},IndexedBatch{11,0,3,300003}}){
        corrupt=indexed;std::memcpy(corrupt.data()+sizeof(Mesh),&invalid,sizeof(invalid));if(invalidCarBody(corrupt))return 72;
    }
    corrupt=indexed;float nonfinite=NAN;std::memcpy(corrupt.data()+sizeof(Mesh)+sizeof(IndexedBatch),&nonfinite,4);
    if(invalidCarBody(corrupt))return 73;
    corrupt=indexed;field=3;std::memcpy(corrupt.data()+sizeof(Mesh)+sizeof(IndexedBatch)+sizeof(v),&field,4);
    if(invalidCarBody(corrupt))return 74;
    Transport bounded({},true);
    for(const VehicleMeshChunk badChunk:{VehicleMeshChunk{3,7,188,0,65537,0},VehicleMeshChunk{3,7,MaxPayload+1,0,1,0},
        VehicleMeshChunk{3,7,188,0xffffffff,1,0},VehicleMeshChunk{3,7,188,0,1,1},VehicleMeshChunk{0,7,188,0,1,0}}){
        auto data=bytes(badChunk);data.push_back(0);if(bounded.accept(Message::VehicleMeshChunk,data))return 75;
    }
    Transport wrapped({},true);auto wrapping=indexed;newFrame=0xffffffff;std::memcpy(wrapping.data()+4,&newFrame,4);
    if(!wrapped.accept(Message::VehicleMeshChunk,vehicleFragment(wrapping,3,0xffffffff,0,static_cast<uint32_t>(wrapping.size()))))return 76;
    newFrame=0;std::memcpy(wrapping.data()+4,&newFrame,4);
    if(!wrapped.accept(Message::VehicleMeshChunk,vehicleFragment(wrapping,3,0,0,static_cast<uint32_t>(wrapping.size())))
       ||wrapped.takeUpdates().meshHeader.frame!=0)return 77;
    // Compact original auxiliary channels retain exact bytes, with no partial
    // publication or format changes during a chunked transfer.
    auto compactPacket=[&](VehicleTextureChunk prefix,std::initializer_list<uint8_t> content){
        auto data=bytes(prefix);data.insert(data.end(),content);return data;
    };
    auto compactFirst=compactPacket({900,2,1,0,1,1,0},{17});
    if(t.accept(Message::VehicleTextureChunk,compactFirst)||!vehicle.accept(Message::VehicleTextureChunk,compactFirst)
       ||!vehicle.takeUpdates().textures.empty())return 78;
    if(vehicle.accept(Message::VehicleTextureChunk,compactPacket({900,2,1,1,1,2,0},{55})))return 79;
    if(!vehicle.accept(Message::VehicleTextureChunk,compactPacket({900,2,1,1,1,1,0},{55})))return 80;
    update=vehicle.takeUpdates();if(update.textures.size()!=1||update.textures[0].format!=1
       ||update.textures[0].rgba!=std::vector<uint8_t>({17,55}))return 81;
    for(uint32_t format:{0U,3U,5U,257U,258U})if(vehicle.accept(Message::VehicleTextureChunk,compactPacket({901,1,1,0,1,format,0},{4})))return 82;
    VehiclePose pose{};pose.epoch=4;pose.vehicleId=42;pose.tick=1;pose.sourceSeconds=.5;
    pose.quaternion[3]=pose.cameraQuaternion[3]=1;pose.cameraY=2;pose.fov=70;pose.cameraMode=1;
    if(t.accept(Message::VehiclePose,bytes(pose))||!vehicle.accept(Message::VehiclePose,bytes(pose)))return 83;
    update=vehicle.takeUpdates();if(update.vehiclePoses.size()!=1||update.vehiclePoses[0].pose.tick!=1)return 84;
    if(!vehicle.accept(Message::VehiclePose,bytes(pose))||!vehicle.takeUpdates().vehiclePoses.empty())return 85;
    pose.tick=2;pose.cameraX=2;
    if(!vehicle.accept(Message::VehiclePose,bytes(pose))||vehicle.takeUpdates().vehiclePoses[0].pose.cameraX!=2)return 86;
    pose.quaternion[3]=0;if(vehicle.accept(Message::VehiclePose,bytes(pose)))return 87;
    pose.quaternion[3]=1;pose.cameraQuaternion[0]=NAN;if(vehicle.accept(Message::VehiclePose,bytes(pose)))return 88;
    // Diagnostic snapshots describe the actual accepted pose, independently
    // of the one-shot queue and connected/freshness authority. A replay must
    // neither replace that pose nor acquire a fresh arrival timestamp.
    Transport poseDiagnostics({},true);
    auto initialPoseSnapshot=poseDiagnostics.takeUpdates();
    if(initialPoseSnapshot.vehicleAgeMs!=UINT32_MAX||initialPoseSnapshot.vehiclePoseAgeMs!=UINT32_MAX
        ||initialPoseSnapshot.latestVehiclePose.epoch||initialPoseSnapshot.vehiclePoseAccepted
        ||initialPoseSnapshot.vehiclePoseIgnored)return 160;
    VehiclePose observed{};observed.epoch=17;observed.vehicleId=20080;observed.tick=3796;observed.sourceSeconds=1779.89414;
    observed.hostX=-20.094;observed.hostY=.18;observed.hostZ=6.983;
    observed.cameraX=-20.094;observed.cameraY=1.5;observed.cameraZ=9.8;
    observed.quaternion[3]=observed.cameraQuaternion[3]=1;observed.fov=70;observed.cameraMode=1;
    if(!poseDiagnostics.accept(Message::VehicleInfo,bytes(VehicleInfo{17,20080,757,1,2.3f,0}))
        ||!poseDiagnostics.accept(Message::VehiclePose,bytes(observed)))return 161;
    auto observedSnapshot=poseDiagnostics.takeUpdates();
    if(observedSnapshot.vehiclePoses.size()!=1||observedSnapshot.vehicleAgeMs==UINT32_MAX
        ||observedSnapshot.vehiclePoseAgeMs==UINT32_MAX||observedSnapshot.vehiclePoseAccepted!=1
        ||observedSnapshot.vehiclePoseIgnored||observedSnapshot.vehicleValid||observedSnapshot.vehiclePoseValid
        ||std::memcmp(&observedSnapshot.latestVehiclePose,&observed,sizeof(observed))
        ||std::memcmp(&observedSnapshot.vehiclePoses[0].pose,&observed,sizeof(observed))
        ||!std::isfinite(observedSnapshot.vehiclePoses[0].arrivalMs)||observedSnapshot.vehiclePoses[0].arrivalMs<=0)return 162;
    Sleep(2);
    auto repeatedPose=observed;repeatedPose.hostX=1234;
    if(!poseDiagnostics.accept(Message::VehiclePose,bytes(repeatedPose)))return 163;
    auto replaySnapshot=poseDiagnostics.takeUpdates();
    if(!replaySnapshot.vehiclePoses.empty()||replaySnapshot.vehiclePoseAccepted!=1||replaySnapshot.vehiclePoseIgnored!=1
        ||replaySnapshot.vehicleAgeMs<=observedSnapshot.vehicleAgeMs||replaySnapshot.vehiclePoseAgeMs<=observedSnapshot.vehiclePoseAgeMs
        ||std::memcmp(&replaySnapshot.latestVehiclePose,&observed,sizeof(observed)))return 164;
    observed.epoch=18;observed.vehicleId=20100;observed.tick=1;observed.sourceSeconds=1780;
    if(!poseDiagnostics.accept(Message::VehiclePose,bytes(observed)))return 165;
    auto renewedPoseSnapshot=poseDiagnostics.takeUpdates();
    if(renewedPoseSnapshot.vehiclePoses.size()!=1||renewedPoseSnapshot.vehiclePoseAccepted!=2
        ||renewedPoseSnapshot.vehiclePoseIgnored!=1||std::memcmp(&renewedPoseSnapshot.latestVehiclePose,&observed,sizeof(observed)))return 166;
    poseDiagnostics.stop();auto resetPoseSnapshot=poseDiagnostics.takeUpdates();
    if(resetPoseSnapshot.vehicleAgeMs!=UINT32_MAX||resetPoseSnapshot.vehiclePoseAgeMs!=UINT32_MAX
        ||resetPoseSnapshot.latestVehiclePose.epoch||resetPoseSnapshot.vehiclePoseAccepted
        ||resetPoseSnapshot.vehiclePoseIgnored||!resetPoseSnapshot.vehiclePoses.empty())return 167;
    VehicleMaterial material{};material.epoch=4;material.id=1;material.layerCount=1;material.alphaRef=.5f;
    VehicleMaterialLayer layer{};layer.opacity=1;layer.roughness=.5f;layer.ao=1;layer.normalStrength=1;
    for(float& color:layer.baseColor)color=1;
    auto materialBytes=[&](){auto data=bytes(material);auto l=bytes(layer);data.insert(data.end(),l.begin(),l.end());return data;};
    if(t.accept(Message::VehicleMaterial,materialBytes()))return 89;
    for(uint32_t blend=0;blend<=5;++blend){material.flags=2|(blend<<8);if(!vehicle.accept(Message::VehicleMaterial,materialBytes()))return 90;}
    update=vehicle.takeUpdates();if(update.vehicleMaterials.size()!=1||update.vehicleMaterials[0].header.flags!=(2|(5<<8)))return 91;
    material.flags=6<<8;if(vehicle.accept(Message::VehicleMaterial,materialBytes()))return 92;
    material.flags=0;layer.roughness=NAN;if(vehicle.accept(Message::VehicleMaterial,materialBytes()))return 93;
    layer.roughness=.5f;layer.mapUV1Mask=1<<11;if(vehicle.accept(Message::VehicleMaterial,materialBytes()))return 94;
    layer.mapUV1Mask=0;layer.maps[0]=65536;if(vehicle.accept(Message::VehicleMaterial,materialBytes()))return 95;
    auto surface=bytes(Mesh{1,1,1,5,4,5,6});VehicleMeshCapture capture{};capture.sourceSeconds=.5;capture.quaternion[3]=1;
    auto captureBytes=bytes(capture);surface.insert(surface.end(),captureBytes.begin(),captureBytes.end());
    auto surfaceBatch=bytes(IndexedVehicleBatch{900,0,3,3,1});surface.insert(surface.end(),surfaceBatch.begin(),surfaceBatch.end());
    const VehicleVertex uvVertices[3]{{0,0,0,0,1,0,0,0,.2f,.3f,0xffffffff},
        {1,0,0,0,1,0,1,0,.4f,.5f,0xffffffff},{0,1,0,0,1,0,0,1,.6f,.7f,0xffffffff}};
    auto sourceVertices=reinterpret_cast<const uint8_t*>(uvVertices);
    surface.insert(surface.end(),sourceVertices,sourceVertices+sizeof(uvVertices));
    for(uint32_t index:{2U,0U,1U}){auto data=bytes(index);surface.insert(surface.end(),data.begin(),data.end());}
    auto surfaceStart=vehicleFragment(surface,5,1,0,37);
    if(t.accept(Message::VehicleSurfaceChunk,surfaceStart)||!vehicle.accept(Message::VehicleSurfaceChunk,surfaceStart)
       ||vehicle.takeUpdates().newMesh)return 96;
    if(vehicle.accept(Message::VehicleMeshChunk,vehicleFragment(surface,5,1,37,1)))return 97;
    if(!vehicle.accept(Message::VehicleSurfaceChunk,vehicleFragment(surface,5,1,37,uint32_t(surface.size())-37)))return 98;
    update=vehicle.takeUpdates();if(!update.newMesh||!update.surfaceMesh||update.meshCapture.sourceSeconds!=.5
       ||update.mesh.size()!=1||update.mesh[0].materialId!=1||update.mesh[0].secondaryUV.size()!=3
       ||update.mesh[0].vertices.size()!=3||update.mesh[0].vertices[0].x!=0||update.mesh[0].vertices[1].x!=1
       ||update.mesh[0].secondaryUV[0][0]!=.2f||update.mesh[0].secondaryUV[1][1]!=.5f
       ||!update.mesh[0].sourceIndices.empty()||update.mesh[0].indices!=std::vector<uint32_t>{2U,0U,1U})return 99;
    corrupt=surface;double badCaptureTime=NAN;std::memcpy(corrupt.data()+sizeof(Mesh),&badCaptureTime,sizeof(badCaptureTime));
    Transport malformedSurface({},true);
    if(malformedSurface.accept(Message::VehicleSurfaceChunk,vehicleFragment(corrupt,5,1,0,uint32_t(corrupt.size()))))return 100;
    // Preserve actual vertex palettes and triangle reuse independently. A
    // >300k-index surface with three real palette vertices used to be rejected
    // by an expanded-vertex limit despite requiring only a small palette.
    auto indexedBody=[&](uint32_t batchCount,uint32_t paletteSize,uint32_t indexCount,bool surfaceFormat=true){
        const size_t vertexSize=surfaceFormat?sizeof(VehicleVertex):sizeof(Vertex);
        const size_t batchSize=surfaceFormat?sizeof(IndexedVehicleBatch):sizeof(IndexedBatch);
        const size_t bodySize=sizeof(Mesh)+(surfaceFormat?sizeof(VehicleMeshCapture):0)
            +size_t(batchCount)*(batchSize+size_t(paletteSize)*vertexSize+size_t(indexCount)*sizeof(uint32_t));
        std::vector<uint8_t> data(bodySize);size_t cursor=0;
        auto append=[&](const auto& value){std::memcpy(data.data()+cursor,&value,sizeof(value));cursor+=sizeof(value);};
        append(Mesh{1,1,batchCount,31,4,5,6});if(surfaceFormat)append(capture);
        for(uint32_t batch=0;batch<batchCount;++batch){
            if(surfaceFormat)append(IndexedVehicleBatch{900+batch,0,paletteSize,indexCount,1});
            else append(IndexedBatch{900+batch,0,paletteSize,indexCount});
            for(uint32_t n=0;n<paletteSize;++n){
                auto actual=uvVertices[n%3];actual.x+=float(n/3)*.001f;
                if(surfaceFormat)append(actual);
                else append(Vertex{actual.x,actual.y,actual.z,actual.nx,actual.ny,actual.nz,actual.u,actual.v,actual.color});
            }
            for(uint32_t n=0;n<indexCount;++n)append(uint32_t((n+2)%3));
        }
        return data;
    };
    bool earlyIndexedPublication=false;
    auto decodeIndexedBody=[&](Message kind,const std::vector<uint8_t>& body,Updates* result=nullptr){
        earlyIndexedPublication=false;
        Transport peer({},true);
        for(uint32_t offset=0;offset<body.size();){
            const uint32_t count=std::min<uint32_t>(64u*1024u,uint32_t(body.size())-offset);
            if(!peer.accept(kind,vehicleFragment(body,31,1,offset,count)))return peer.takeUpdates().newMesh;
            offset+=count;
            if(offset<body.size()&&peer.takeUpdates().newMesh){earlyIndexedPublication=true;return true;}
        }
        auto decoded=peer.takeUpdates();const bool complete=decoded.newMesh
            &&decoded.surfaceMesh==(kind==Message::VehicleSurfaceChunk);
        if(result)*result=std::move(decoded);
        return complete;
    };
    {
        auto body=indexedBody(1,3,MaxVertices+3);Updates decoded;
        if(!decodeIndexedBody(Message::VehicleSurfaceChunk,body,&decoded)||earlyIndexedPublication||decoded.mesh.size()!=1
           ||decoded.mesh[0].vertices.size()!=3||decoded.mesh[0].secondaryUV.size()!=3
           ||decoded.mesh[0].indices.size()!=MaxVertices+3||!decoded.mesh[0].sourceIndices.empty())return 170;
        for(size_t n=0;n<decoded.mesh[0].indices.size();++n)if(decoded.mesh[0].indices[n]!=(n+2)%3)return 171;
        for(size_t n=0;n<3;++n){const auto& actual=decoded.mesh[0].vertices[n];const auto& expected=uvVertices[n];
            if(actual.x!=expected.x||actual.y!=expected.y||actual.u!=expected.u||actual.v!=expected.v
               ||actual.color!=expected.color||decoded.mesh[0].secondaryUV[n]!=std::array<float,2>{expected.u2,expected.v2})return 172;}
    }
    {
        auto body=indexedBody(1,3,MaxVehicleIndices);Updates decoded;
        if(!decodeIndexedBody(Message::VehicleSurfaceChunk,body,&decoded)||earlyIndexedPublication||decoded.mesh.size()!=1
           ||decoded.mesh[0].vertices.size()!=3||decoded.mesh[0].indices.size()!=MaxVehicleIndices)return 173;
    }
    if(decodeIndexedBody(Message::VehicleSurfaceChunk,indexedBody(1,3,MaxVehicleIndices+3)))return 174;
    {
        auto body=indexedBody(2,MaxVehicleVertices/2,MaxVehicleVertices/2);Updates decoded;
        if(!decodeIndexedBody(Message::VehicleSurfaceChunk,body,&decoded)||earlyIndexedPublication||decoded.mesh.size()!=2
           ||decoded.mesh[0].vertices.size()+decoded.mesh[1].vertices.size()!=MaxVehicleVertices)return 175;
    }
    // Each individual batch fits: only the complete frame's aggregate palette
    // budget is exceeded. Its whole structurally valid body is present.
    if(decodeIndexedBody(Message::VehicleSurfaceChunk,indexedBody(2,MaxVehicleVertices/2+3,MaxVehicleVertices/2+3)))return 176;
    {
        auto body=indexedBody(MaxVehicleBatches,3,3);Updates decoded;
        if(!decodeIndexedBody(Message::VehicleSurfaceChunk,body,&decoded)||earlyIndexedPublication||decoded.mesh.size()!=MaxVehicleBatches)return 177;
    }
    if(decodeIndexedBody(Message::VehicleSurfaceChunk,indexedBody(MaxVehicleBatches+1,3,3)))return 178;
    // The larger vehicle index budget cannot enlarge either legacy Type10 or
    // the Minecraft expanded-vertex/category decoder.
    if(decodeIndexedBody(Message::VehicleMeshChunk,indexedBody(1,3,MaxVertices+3,false)))return 179;
    {
        auto data=bytes(Mesh{1,1,1,31,4,5,6});auto prefix=bytes(Batch{900,0,MaxVertices+3});
        data.insert(data.end(),prefix.begin(),prefix.end());const size_t base=data.size();data.resize(base+size_t(MaxVertices+3)*sizeof(Vertex));
        for(uint32_t n=0;n<MaxVertices+3;++n)std::memcpy(data.data()+base+size_t(n)*sizeof(Vertex),&v[n%3],sizeof(Vertex));
        Transport minecraft;if(minecraft.accept(Message::Mesh,data))return 180;
    }
    const auto validTwoBatches=indexedBody(2,3,3);
    const size_t surfaceHeader=sizeof(Mesh)+sizeof(VehicleMeshCapture);
    const size_t secondBatch=surfaceHeader+sizeof(IndexedVehicleBatch)+3*sizeof(VehicleVertex)+3*sizeof(uint32_t);
    // Failures in the last batch or final index never publish the earlier
    // otherwise valid batch. All offsets refer to complete actual body bytes.
    {
        auto body=validTwoBatches;uint32_t outsidePalette=3;
        std::memcpy(body.data()+body.size()-sizeof(outsidePalette),&outsidePalette,sizeof(outsidePalette));
        if(decodeIndexedBody(Message::VehicleSurfaceChunk,body))return 181;
    }
    {
        auto body=validTwoBatches;float invalidUV=NAN;
        std::memcpy(body.data()+secondBatch+sizeof(IndexedVehicleBatch)+offsetof(VehicleVertex,u2),&invalidUV,sizeof(invalidUV));
        if(decodeIndexedBody(Message::VehicleSurfaceChunk,body))return 182;
    }
    {
        auto body=validTwoBatches;uint32_t unknownMaterial=257;
        std::memcpy(body.data()+secondBatch+offsetof(IndexedVehicleBatch,materialId),&unknownMaterial,sizeof(unknownMaterial));
        if(decodeIndexedBody(Message::VehicleSurfaceChunk,body))return 183;
    }
    {
        auto body=validTwoBatches;body.pop_back();if(decodeIndexedBody(Message::VehicleSurfaceChunk,body))return 184;
        body=validTwoBatches;body.push_back(0);if(decodeIndexedBody(Message::VehicleSurfaceChunk,body))return 185;
    }
    {
        Transport peer({},true);auto body=indexedBody(1,3,MaxVertices+3);
        if(!peer.accept(Message::VehicleSurfaceChunk,vehicleFragment(body,31,1,0,37))||peer.takeUpdates().newMesh
           ||peer.accept(Message::VehicleSurfaceChunk,vehicleFragment(body,31,1,38,1))
           ||peer.accept(Message::VehicleSurfaceChunk,vehicleFragment(body,31,1,0,37)))return 186;
        for(uint32_t offset=37;offset<body.size();){
            uint32_t count=std::min<uint32_t>(64u*1024u,uint32_t(body.size())-offset);
            if(!peer.accept(Message::VehicleSurfaceChunk,vehicleFragment(body,31,1,offset,count)))return 187;offset+=count;
        }
        auto decoded=peer.takeUpdates();if(!decoded.newMesh||decoded.mesh[0].indices.size()!=MaxVertices+3
           ||peer.accept(Message::VehicleSurfaceChunk,vehicleFragment(body,31,1,0,37)))return 188;
        auto priorEpoch=body;uint32_t oldEpoch=30;std::memcpy(priorEpoch.data()+offsetof(Mesh,epoch),&oldEpoch,sizeof(oldEpoch));
        if(peer.accept(Message::VehicleSurfaceChunk,vehicleFragment(priorEpoch,30,2,0,37))||peer.takeUpdates().newMesh)return 189;
    }
    {
        Transport peer({},true);
        for(const VehicleMeshChunk invalid:{VehicleMeshChunk{31,1,MaxVehicleSurfaceBytes+1,0,1,0},
            VehicleMeshChunk{31,1,MaxVehicleSurfaceBytes,0xffffffff,1,0},VehicleMeshChunk{31,1,MaxVehicleSurfaceBytes,0,1,1}}){
            auto fragment=bytes(invalid);fragment.push_back(0);
            if(peer.accept(Message::VehicleSurfaceChunk,fragment)||peer.takeUpdates().newMesh)return 190;
        }
    }
    VehicleDashboard dashboard{};dashboard.epoch=5;dashboard.vehicleId=42;dashboard.sourceSeconds=.5;
    dashboard.speed=20;dashboard.rpm=4000;dashboard.maxRPM=7000;dashboard.gearIndex=3;
    dashboard.fuel=40;dashboard.fuelCapacity=50;dashboard.flags=0xff7f;dashboard.engineTemperature=90;
    if(t.accept(Message::VehicleDashboard,bytes(dashboard))||!vehicle.accept(Message::VehicleDashboard,bytes(dashboard)))return 101;
    update=vehicle.takeUpdates();if(!update.newVehicleDashboard||update.vehicleDashboard.rpm!=4000)return 102;
    dashboard.flags|=128;if(vehicle.accept(Message::VehicleDashboard,bytes(dashboard)))return 103;
    dashboard.flags&=~128u;dashboard.fuel=51;if(vehicle.accept(Message::VehicleDashboard,bytes(dashboard)))return 104;
    dashboard.fuel=40;dashboard.reserved[2]=1;if(vehicle.accept(Message::VehicleDashboard,bytes(dashboard)))return 105;
    // DDS-declared sRGB is a sampling flag, not 260 bytes per pixel.
    if(!vehicle.accept(Message::VehicleTextureChunk,compactPacket({902,1,1,0,4,260,0},{31,47,63,255})))return 106;
    update=vehicle.takeUpdates();if(update.textures.size()!=1||update.textures[0].format!=260
       ||update.textures[0].rgba!=std::vector<uint8_t>({31,47,63,255}))return 107;
    // Preserve source depth-write and black reflection independently of blending.
    layer.maps[0]=0;material.flags=2|8|16|(1<<8);
    if(!vehicle.accept(Message::VehicleMaterial,materialBytes()))return 108;
    update=vehicle.takeUpdates();if(update.vehicleMaterials.size()!=1
       ||update.vehicleMaterials[0].header.flags!=material.flags)return 109;
    material.flags|=32;if(vehicle.accept(Message::VehicleMaterial,materialBytes()))return 110;
    // Revisions replace only their undrained completed candidate. Test the
    // actual 384MiB arithmetic without allocating hundreds of megabytes.
    constexpr size_t MiB=1024u*1024u;
    if(!vehicleTextureRevisionFits(384*MiB,0,256,0,64*MiB,true,64*MiB)||
       vehicleTextureRevisionFits(384*MiB,0,256,0,64*MiB,true,64*MiB+1)||
       vehicleTextureRevisionFits(384*MiB,0,256,0,0,false,1))return 111;
    if(!vehicleTextureRevisionFits(320*MiB,64*MiB,20,2,64*MiB,true,64*MiB)||
       vehicleTextureRevisionFits(320*MiB,64*MiB,20,2,64*MiB,true,64*MiB+1)||
       vehicleTextureRevisionFits(1,0,1,8,1,true,1))return 112;
    if(vehicleTextureRevisionFits(4,0,1,0,5,true,1)||
       vehicleTextureRevisionFits(0,0,0,0,0,true,1)||
       vehicleTextureRevisionFits(SIZE_MAX,0,1,0,0,false,1)||
       vehicleTextureRevisionFits(1,0,SIZE_MAX,0,0,false,1))return 113;
    if(!vehicleTextureRevisionFits(255,1,255,1,1,true,1)||
       vehicleTextureRevisionFits(254,2,254,2,0,false,1))return 114;
    Transport revision({},true);
    if(!revision.accept(Message::VehicleTextureChunk,compactPacket({1000,2,1,0,8,4,0},{1,2,3,4,5,6,7,8}))||
       !revision.accept(Message::VehicleTextureChunk,compactPacket({1001,1,1,0,4,4,0},{9,10,11,12}))||
       !revision.accept(Message::VehicleTextureChunk,compactPacket({1002,2,1,0,1,1,0},{7})))return 115;
    if(!revision.accept(Message::VehicleTextureChunk,compactPacket({1000,2,1,0,1,1,0},{17})))return 116;
    auto revised=revision.takeUpdates();
    if(revised.textures.size()!=1||revised.textures[0].id!=1001||
       revised.textures[0].rgba!=std::vector<uint8_t>({9,10,11,12}))return 117;
    if(revision.accept(Message::VehicleTextureChunk,compactPacket({1000,3,1,1,1,1,0},{55}))||
       revision.accept(Message::VehicleTextureChunk,compactPacket({1000,2,1,1,1,2,0},{55}))||
       revision.accept(Message::VehicleTextureChunk,compactPacket({1000,2,1,0,1,1,0},{55}))||
       revision.accept(Message::VehicleTextureChunk,compactPacket({1002,2,1,0,1,1,0},{9})))return 118;
    if(!revision.accept(Message::VehicleTextureChunk,compactPacket({1000,2,1,1,1,1,0},{55}))||
       !revision.accept(Message::VehicleTextureChunk,compactPacket({1002,2,1,1,1,1,0},{9})))return 119;
    revised=revision.takeUpdates();
    if(revised.textures.size()!=2||revised.textures[0].id!=1000||revised.textures[0].format!=1||
       revised.textures[0].rgba!=std::vector<uint8_t>({17,55})||revised.textures[1].id!=1002||
       revised.textures[1].rgba!=std::vector<uint8_t>({7,9}))return 120;
    // The same-ID candidate also frees exactly one queue entry at the real
    // 256-image bound. Every other one-byte image must survive unchanged.
    Transport fullImages({},true);
    for(uint32_t id=2000;id<2256;++id)if(!fullImages.accept(Message::VehicleTextureChunk,compactPacket({id,1,1,0,1,1,0},{4})))return 121;
    if(fullImages.accept(Message::VehicleTextureChunk,compactPacket({2300,1,1,0,1,1,0},{4}))||
       !fullImages.accept(Message::VehicleTextureChunk,compactPacket({2000,2,1,0,1,1,0},{31})))return 122;
    revised=fullImages.takeUpdates();if(revised.textures.size()!=255)return 123;
    for(size_t i=0;i<revised.textures.size();++i)if(revised.textures[i].id!=2001+i||revised.textures[i].rgba!=std::vector<uint8_t>({4}))return 124;
    if(!fullImages.accept(Message::VehicleTextureChunk,compactPacket({2000,2,1,1,1,1,0},{47})))return 125;
    revised=fullImages.takeUpdates();if(revised.textures.size()!=1||revised.textures[0].id!=2000||
       revised.textures[0].rgba!=std::vector<uint8_t>({31,47}))return 126;
    // An invalid replacement start never retires a valid old candidate.
    Transport invalidRevision({},true);
    if(!invalidRevision.accept(Message::VehicleTextureChunk,compactPacket({1004,2,1,0,2,1,0},{8,9})))return 127;
    if(invalidRevision.accept(Message::VehicleTextureChunk,compactPacket({1004,4097,1,0,1,1,0},{4}))||
       invalidRevision.accept(Message::VehicleTextureChunk,compactPacket({1004,2,1,0,1,257,0},{4}))||
       invalidRevision.accept(Message::VehicleTextureChunk,compactPacket({1004,2,1,0,1,1,1},{4}))||
       invalidRevision.accept(Message::VehicleTextureChunk,compactPacket({1004,2,1,1,1,1,0},{4}))||
       invalidRevision.accept(Message::VehicleTextureChunk,compactPacket({1004,1,1,0,2,1,0},{4,5})))return 128;
    revised=invalidRevision.takeUpdates();if(revised.textures.size()!=1||revised.textures[0].id!=1004||
       revised.textures[0].rgba!=std::vector<uint8_t>({8,9}))return 129;
    if(const int compact=compactTextureFixture())return compact;
    return allocationRecoveryFixture();
}
extern "C" __declspec(dllexport) int __cdecl RE4CraftTransportSelfTest(uint16_t port,uint32_t timeoutMs){
    using namespace re4craft;
    Transport t;Input input{};input.flags=Enabled|Focused;input.held=Forward;input.selectedSlot=0xffffffff;t.setInput(input);t.start(port);
    bool state=false,texture=false,mesh=false;
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
    while(std::chrono::steady_clock::now()<deadline){
        Updates u=t.takeUpdates();
        if(u.stateValid&&u.state.tick==12345)state=true;
        for(const auto& image:u.textures)if(image.id==7&&image.width==2&&image.height==2&&image.rgba.size()==16)texture=true;
        if(u.newMesh&&u.mesh.size()==1&&u.mesh[0].textureId==7&&u.mesh[0].vertices.size()==3)mesh=true;
        if(state&&texture&&mesh){t.stop();return 0;}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    t.stop();return (state?0:1)|(texture?0:2)|(mesh?0:4);
}
