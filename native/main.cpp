#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "vendor/minhook/include/MinHook.h"
#include "game_access.hpp"
#include "renderer.hpp"
#include "transport.hpp"
#include "camera_control.hpp"
#include "host_controls.hpp"
#include "host_input.hpp"
#include "hud_renderer.hpp"
#include "collision_export.hpp"
#include "guest_controller.hpp"
#include "world_render.hpp"
#include "hook_support.hpp"
#include "d3d_hook_transaction.hpp"
#include "d3d_device_snapshot.hpp"
#include "beamng_mode.hpp"
#include "leon_protection.hpp"
#include "native_grab_hooks.hpp"
#include "native_window_hooks.hpp"
#include "native_window_handoff.hpp"
#include "native_script_handoff.hpp"
#include "vehicle_presentation.hpp"
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <mutex>
#include <string>
#include <cstring>
#include <chrono>
#include <algorithm>

namespace {
using namespace re4craft;
using EndSceneFn=HRESULT(WINAPI*)(IDirect3DDevice9*);
using ResetFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using PresentFn=HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
EndSceneFn originalEndScene{};ResetFn originalReset{};PresentFn originalPresent{};
std::atomic<bool> graphicsReady{false};
HMODULE pluginModule{};
std::mutex logMutex,renderMutex;
std::wstring root;
FILE* logFile{};
GameAccess* game{};Renderer* renderer{};Transport* transport{};
struct GuestBackend {
    BridgeMode mode;const wchar_t* section;const char* name;uint16_t port;int toggleKey;
    Renderer* renderer{};Transport* transport{};HudRenderer* hud{};
};
std::array<GuestBackend,GuestBackendCount> backends{{
    {BridgeMode::Minecraft,L"Bridge","Minecraft",Port,VK_F8},
    {BridgeMode::BeamNG,L"BeamNG","BeamNG",18047,VK_F7},
    {BridgeMode::Halo,L"Halo","Halo 1",18048,VK_F6},
    {BridgeMode::Ultrakill,L"Ultrakill","ULTRAKILL",18049,VK_F4}
}};
GuestBackend* backendFor(BridgeMode selected){
    const auto index=guestBackendIndex(selected);return index<backends.size()?&backends[index]:nullptr;
}
VehiclePresentation carPresentation;
BeamngHeldPresentation heldCarPresentation;
BridgeMode mode=BridgeMode::Native,selectedBackend=BridgeMode::Minecraft;
uint32_t avatarEpoch=0xffffffff;
CameraControlBridge* camera{};HostControls* controls{};HostInput* hostInput{};HudRenderer* hud{};CollisionExporter* collision{};
GuestController* guest{};
bool cameraReady{},controlsReady{},collisionReady{},guestReady{},worldReady{};
uint32_t collisionEpoch=0xffffffff;Vec3 collisionCenter{};ULONGLONG collisionTime{};
ULONGLONG collisionAcceptedTime{};
bool enabled=false,reportedMesh=false,reportedDiagnostics=false;
std::array<bool,256> previous{};
std::array<ULONGLONG,256> lastKeyEdge{};
uint16_t lastRoom=0xffff;uint32_t epoch=0;
int beamngMenuKey='V';
int haloCrouchKey=VK_LCONTROL,haloReloadKey='R',haloGrenadeKey='F',haloMeleeKey='V',haloUseKey='E',haloWeaponKey=VK_TAB;
LeonProtectionPolicy leonProtection;
NativeGrabHooks nativeGrabs;
NativeWindowHooks nativeWindows;
NativeWindowHandoff windowHandoff;
NativeScriptHandoff scriptHandoff;
std::mutex windowMutex;
struct WindowContext {BridgeMode mode{BridgeMode::Native};uint32_t epoch{};float units{1000.f};bool requested{};uint64_t sourceFrame{},updatedAt{};} windowContext;
uint32_t beamngMenuEpoch=0xffffffff;bool beamngMenuSeen=false;
float units=1000.f,ySign=1.f,feetOffset=0.f,terrainRadius=256.f;
bool hostAuthority=false;
bool collisionComplete=false;
Collision capturedTerrain{};
std::vector<CollisionInstance> movingSurfaceBaseline;
uint32_t movingSurfaceRevision{};ULONGLONG movingSurfaceTime{};
int forwardKey='Z',backKey='S',leftKey='Q',rightKey='D',sneakKey=VK_LSHIFT,sprintKey=VK_LCONTROL,inventoryKey='X',dropKey='A',chatKey='T';
ULONGLONG enemySendTime{};
RenderPlacement worldPlacement{};
bool worldDesired{};
ULONGLONG worldHeartbeat{},worldSuccessTime{},worldBootstrapTime{};
uint32_t worldSuccessEpoch=0xffffffff;
struct FrameCosts {double total{},hud{},ingest{},terrain{},world{},avatarAge{},frameMax{},hudMax{},terrainMax{},worldMax{};uint32_t frames{},worldFrames{},avatarUpdates{},avatarAgeSamples{},avatarAgeMax{};ULONGLONG since{};} frameCosts;
using PerfClock=std::chrono::steady_clock;
IDirect3DDevice9* measuredDevice{};
struct PresentCosts {PerfClock::time_point last{};double intervals{},smallest{1e9},largest{},callTime{};uint32_t frames{},samples{},bridgeFrames{};ULONGLONG since{};} presentCosts;
double milliseconds(PerfClock::time_point start){return std::chrono::duration<double,std::milli>(PerfClock::now()-start).count();}
void logMessage(const char* text){
    std::lock_guard lock(logMutex);
    if(logFile){SYSTEMTIME time;GetLocalTime(&time);fprintf(logFile,"[%02u:%02u:%02u] %s\n",time.wHour,time.wMinute,time.wSecond,text);fflush(logFile);}
}
bool down(int key){return key>=0&&key<256&&(GetAsyncKeyState(key)&0x8000)!=0;}
// Paused/cancelled additions stay preserved but do not run or capture keys.
constexpr bool backendAvailable(BridgeMode backend){return backend==BridgeMode::Minecraft||backend==BridgeMode::BeamNG;}
void cancelWindowHandoff(){
    std::lock_guard lock(windowMutex);windowHandoff.cancel();scriptHandoff.cancel();windowContext={};
    if(guest)guest->setNativeTraversalHold(false);
}
uint64_t beginNativeWindow(void*){
    if(!game||!guest)return 0;
    const auto state=game->traversalState();uint64_t token{};
    {std::lock_guard lock(windowMutex);const auto now=GetTickCount64();token=windowHandoff.begin(windowContext.mode,
        !scriptHandoff.blocksGuestMotion()&&windowContext.requested&&now>=windowContext.updatedAt&&now-windowContext.updatedAt<250,state,windowContext.epoch,now,windowContext.units);
        if(token)guest->setNativeTraversalHold(true);}
    if(token)logMessage("Minecraft window traversal: original RE4 action owns movement; source anchor retained.");
    return token;
}
void completeNativeWindow(void*,uint64_t token,bool succeeded){
    if(!token||!game||!guest)return;
    const auto state=game->traversalState();bool completed{};
    {std::lock_guard lock(windowMutex);completed=windowHandoff.complete(token,succeeded,state,windowContext.sourceFrame,GetTickCount64());}
    logMessage(completed?"Minecraft window traversal: native landing recorded; awaiting actual client/server source receipt.":
        "Minecraft window traversal: original action failed or identity changed; native fallback required.");
}
bool edge(int key){
    if(key<=0||key>=256)return false;
    const bool value=down(key),message=hostInput&&hostInput->consumeKeyPress(key);
    const bool result=(value&&!previous[key])||message;previous[key]=value;
    // An event and the asynchronous sample may straddle two EndScene calls.
    // Coalesce that one press while retaining short event-only key taps.
    const auto now=GetTickCount64();if(!result||(lastKeyEdge[key]&&now-lastKeyEdge[key]<80))return false;
    lastKeyEdge[key]=now;return true;
}
bool focused(){DWORD pid{};GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
void loadSettings(){
    auto ini=root+L"RE4Craft.ini";
    const float previousUnits=units,previousRadius=terrainRadius;
    for(auto& backend:backends){
        int candidate=GetPrivateProfileIntW(backend.section,L"Port",backend.port,ini.c_str());
        if(candidate>1024&&candidate<65536&&std::none_of(backends.begin(),backends.end(),[&](const GuestBackend& other){return &other!=&backend&&other.port==candidate;}))backend.port=static_cast<uint16_t>(candidate);
        candidate=GetPrivateProfileIntW(backend.section,L"ToggleKey",backend.toggleKey,ini.c_str());
        if(candidate>0&&candidate<256&&candidate!=VK_F2&&std::none_of(backends.begin(),backends.end(),[&](const GuestBackend& other){return &other!=&backend&&other.toggleKey==candidate;}))backend.toggleKey=candidate;
    }
    int value=GetPrivateProfileIntW(L"BeamNG",L"MenuKey",'V',ini.c_str());if(value>0&&value<256&&std::none_of(backends.begin(),backends.end(),[&](const GuestBackend& backend){return backend.toggleKey==value;})&&value!='R'&&value!='C'&&value!=VK_ESCAPE&&value!=VK_F5)beamngMenuKey=value;
    value=GetPrivateProfileIntW(L"Bridge",L"WorldUnitsPerBlock",1000,ini.c_str());if(value>0&&value<10000)units=static_cast<float>(value);
    value=GetPrivateProfileIntW(L"Bridge",L"TerrainRadius",256,ini.c_str());terrainRadius=static_cast<float>(std::clamp(value,32,256));
    if(previousUnits!=units||previousRadius!=terrainRadius)collisionEpoch=0xffffffff;
    ySign=GetPrivateProfileIntW(L"Bridge",L"YSign",1,ini.c_str())<0?-1.f:1.f;
    feetOffset=static_cast<float>(GetPrivateProfileIntW(L"Bridge",L"FeetOffset",0,ini.c_str()));
    hostAuthority=GetPrivateProfileIntW(L"Bridge",L"HostAuthority",0,ini.c_str())!=0;
    const std::pair<const wchar_t*,int*> bindings[]{{L"Forward",&forwardKey},{L"Back",&backKey},{L"Left",&leftKey},{L"Right",&rightKey},{L"Sneak",&sneakKey},{L"Sprint",&sprintKey},{L"Inventory",&inventoryKey},{L"Drop",&dropKey},{L"Chat",&chatKey}};
    for(auto [name,key]:bindings){int binding=GetPrivateProfileIntW(L"Controls",name,*key,ini.c_str());if(binding>0&&binding<256)*key=binding;}
    const std::pair<const wchar_t*,int*> haloBindings[]{{L"Crouch",&haloCrouchKey},{L"Reload",&haloReloadKey},{L"Grenade",&haloGrenadeKey},{L"Melee",&haloMeleeKey},{L"Use",&haloUseKey},{L"Weapon",&haloWeaponKey}};
    for(auto [name,key]:haloBindings){int binding=GetPrivateProfileIntW(L"Halo",name,*key,ini.c_str());if(binding>0&&binding<256&&binding!=VK_F2&&std::none_of(backends.begin(),backends.end(),[&](const GuestBackend& backend){return backend.toggleKey==binding;}))*key=binding;}
}
void configure(){
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(pluginModule,path,MAX_PATH);root=path;
    auto split=root.find_last_of(L"\\/");root=root.substr(0,split+1);
    logFile=_wfsopen((root+L"RE4Craft.log").c_str(),L"a",_SH_DENYNO);
    loadSettings();
}
bool drawWorld(IDirect3DDevice9* device,const NativeSnapshot& native){
    std::lock_guard lock(renderMutex);
    const auto checkedAt=GetTickCount64();
    const bool foreground=focused(),replaceable=native.canReplace();
    auto reject=[&](const char* reason,const AppliedVehicleCameraDiagnostic* pair=nullptr,bool cameraActive=false,bool submitted=false,bool worldValid=false){
        static uint32_t rejectionEpoch=0xffffffff;static unsigned rejectedLogs{};static ULONGLONG lastLog{};
        if(rejectionEpoch!=epoch){rejectionEpoch=epoch;rejectedLogs=0;lastLog=0;}
        if(rejectedLogs>=16||(lastLog&&checkedAt-lastLog<100))return;
        ++rejectedLogs;lastLog=checkedAt;
        char line[1536]{};
        if(pair){
            sprintf_s(line,"World draw rejected: reason=%s epoch=%u backend=%u cameraActive=%u nativeValid=%u history(total/epoch/fresh)=%zu/%zu/%zu candidate=%u fresh=%u tick=%llu age=%.3fms delta(pos/target/fov)=%.6f/%.6f/%.6f upDot=%.9f upNorm=%.9f nativePos=(%.6f,%.6f,%.6f) nativeTarget=(%.6f,%.6f,%.6f) nativeFov=%.6f nativeUp=(%.9f,%.9f,%.9f) authoredPos=(%.6f,%.6f,%.6f) authoredTarget=(%.6f,%.6f,%.6f) authoredFov=%.6f authoredUp=(%.9f,%.9f,%.9f).",
                reason,epoch,unsigned(selectedBackend),unsigned(cameraActive),unsigned(pair->nativeValid),pair->total,pair->sameEpoch,pair->fresh,
                unsigned(pair->candidate),unsigned(pair->candidateFresh),pair->tick,pair->ageMs,pair->positionDelta,pair->targetDelta,pair->fovDelta,pair->upDot,pair->nativeUpLength,
                native.cameraPosition.x,native.cameraPosition.y,native.cameraPosition.z,native.cameraTarget.x,native.cameraTarget.y,native.cameraTarget.z,native.fovDegrees,native.view[1][0],native.view[1][1],native.view[1][2],
                pair->position.x,pair->position.y,pair->position.z,pair->target.x,pair->target.y,pair->target.z,pair->fov,pair->up.x,pair->up.y,pair->up.z);
        }else{
            sprintf_s(line,"World draw rejected: reason=%s epoch=%u backend=%u wanted=%u enabled=%u focused=%u heartbeatAge=%llums nativeReplaceable=%u room=%X/%X submitted=%u worldValid=%u.",
                reason,epoch,unsigned(selectedBackend),unsigned(worldDesired),unsigned(enabled),unsigned(foreground),checkedAt-worldHeartbeat,unsigned(replaceable),native.room,lastRoom,unsigned(submitted),unsigned(worldValid));
        }
        logMessage(line);
    };
    if(!worldDesired||!enabled||!foreground||checkedAt-worldHeartbeat>250||!replaceable||native.room!=lastRoom){reject("preconditions");return false;}
    auto perfStart=PerfClock::now();
    if(selectedBackend==BridgeMode::BeamNG){
        if(!camera){reject("camera-unavailable");return false;}
        VehiclePresentationSample applied{};
        if(camera->appliedVehiclePresentation(native,epoch,applied)){
            renderer->setVehiclePose({float(applied.position.x*units),float(applied.position.y*units*ySign)+feetOffset,float(applied.position.z*units)},applied.orientation,epoch,applied.vehicleId,applied.sourceSeconds);
            static uint32_t pairedEpoch=0xffffffff;static unsigned pairLogs{};
            if(pairedEpoch!=epoch&&pairLogs<64){char line[224]{};
                sprintf_s(line,"BeamNG visual pose paired with applied native world camera: epoch=%u tick=%llu source=%.6f s bodyBuffer=%.0f..%.0f ms cameraBuffer=%.0f ms.",epoch,applied.tick,applied.sourceSeconds,VehiclePresentation::DelayMs,VehiclePresentation::MaxDelayMs,VehiclePresentation::CameraDelayMs);
                logMessage(line);pairedEpoch=epoch;++pairLogs;
            }
        }else{
            bool cameraActive{};const auto detail=camera->appliedVehicleDiagnostic(native,epoch,cameraActive);
            reject("applied-camera-mismatch",&detail,cameraActive);return false;
        }
    }
    if(!reportedDiagnostics){logMessage(renderer->describeFrame(device,native,worldPlacement).c_str());reportedDiagnostics=true;}
    bool worldValid=false;
    const bool submitted=renderer->render(device,native,worldPlacement,&worldValid);
    const double worldCost=milliseconds(perfStart);frameCosts.world+=worldCost;frameCosts.worldMax=std::max(frameCosts.worldMax,worldCost);frameCosts.worldFrames++;
    bool okay=worldValid&&(selectedBackend!=BridgeMode::BeamNG||submitted);
    if(!okay)reject("renderer",nullptr,false,submitted,worldValid);
    if(okay){worldSuccessTime=GetTickCount64();worldSuccessEpoch=epoch;}
    return okay;
}
void frame(IDirect3DDevice9* device){
    if(!game||!renderer||!transport)return;
    const auto perfStart=PerfClock::now();
    NativeSnapshot native=game->sample();bool foreground=focused();
    if(native.valid)measuredDevice=device;
    std::array<bool,GuestBackendCount> toggles{};
    for(size_t i=0;i<backends.size();++i){const bool pressed=edge(backends[i].toggleKey);toggles[i]=backendAvailable(backends[i].mode)&&pressed&&!(backends[i].toggleKey==VK_F4&&down(VK_MENU));}
    const BridgeMode nextMode=foreground?selectedBridgeMode(mode,toggles[3],toggles[2],toggles[1],toggles[0]):mode;
    if(nextMode!=mode){
        cancelWindowHandoff();
        loadSettings();guest->restore();camera->restore();controls->restore();hostInput->setActive(false,false);
        Input release{};release.selectedSlot=0xffffffff;release.hostEpoch=epoch;
        for(auto& backend:backends)backend.transport->setInput(release);
        mode=nextMode;enabled=mode!=BridgeMode::Native;
        carPresentation.clear();heldCarPresentation.clear();
        for(auto& backend:backends)backend.hud->clear();
        if(enabled)selectedBackend=mode;
        if(auto* selected=backendFor(selectedBackend)){transport=selected->transport;renderer=selected->renderer;hud=selected->hud;}
        ++epoch;avatarEpoch=0xffffffff;collisionEpoch=0xffffffff;collisionComplete=false;frameCosts={};
        worldDesired=false;worldSuccessTime=worldBootstrapTime=0;reportedMesh=false;reportedDiagnostics=false;
        if(!enabled)logMessage("All guest bridges disabled; restored original player");
        else if(auto* selected=backendFor(mode)){char line[256]{};sprintf_s(line,"%s mode enabled; other guest controls released. Awaiting actual source geometry, camera, motion and collision readiness.",selected->name);logMessage(line);}
    }
    const bool vehicleMode=selectedBackend==BridgeMode::BeamNG;
    const bool minecraftMode=selectedBackend==BridgeMode::Minecraft;
    const bool haloMode=selectedBackend==BridgeMode::Halo;
    const bool sourceMode=modernGuestMode(selectedBackend);
    if(leonProtection.handleF2Edge(mode,foreground,edge(VK_F2)))
        logMessage(leonProtection.requested()?"F2 Leon protection ON: native Leon infinite health and common grab protection.":"F2 Leon protection OFF.");
    guest->setLeonProtection(leonProtection.requested(),mode);
    // All socket sessions stay connected. Drain dormant textures into their
    // own cache; no texture IDs, GUI events or authority are shared between games.
    Input dormantInput{};dormantInput.selectedSlot=0xffffffff;dormantInput.hostEpoch=epoch;
    for(auto& backend:backends){
        if(!backendAvailable(backend.mode)||backend.mode==selectedBackend)continue;
        backend.transport->setInput(dormantInput);auto dormant=backend.transport->takeUpdates();
        if(dormant.newSession){backend.renderer->clear();backend.hud->clear();if(backend.mode==BridgeMode::BeamNG){carPresentation.clear();heldCarPresentation.clear();}}
        backend.renderer->retirePendingResources();
        for(auto& tex:dormant.textures){if(backend.mode==BridgeMode::BeamNG)backend.renderer->uploadVehicleTexture(tex.id,tex.width,tex.height,std::move(tex.rgba),tex.format,tex.compressed);else backend.renderer->uploadTexture(tex.id,tex.width,tex.height,std::move(tex.rgba),tex.format);}
        if(backend.mode==BridgeMode::BeamNG)for(const auto& material:dormant.vehicleMaterials)backend.renderer->setVehicleMaterial(material.header,material.layers);
    }
    Updates updates=transport->takeUpdates();
    if(native.valid&&native.room!=lastRoom){lastRoom=native.room;epoch++;for(auto& backend:backends)backend.hud->clear();carPresentation.clear();heldCarPresentation.clear();worldSuccessTime=worldBootstrapTime=0;collisionEpoch=0xffffffff;collisionComplete=false;}
    if(updates.newSession){renderer->clear();hud->clear();carPresentation.clear();heldCarPresentation.clear();avatarEpoch=0xffffffff;collisionComplete=false;reportedMesh=false;reportedDiagnostics=false;worldSuccessTime=worldBootstrapTime=0;epoch++;}
    const auto windowState=game->traversalState();
    // Camera data may be unavailable during a native movie; the scalar-only
    // witness still reports the actual STA_EVENT begin/end ownership.
    const auto scriptState=native.valid?native:game->protectionState();
    WindowHandoffPhase windowPhase{},scriptPhase{};NativeTraversalState windowLanding{},scriptLanding{};
    bool windowFailed=false,windowAcknowledged=false,scriptFailed=false,scriptAcknowledged=false,scriptBegan=false,scriptLanded=false;
    {
        std::lock_guard lock(windowMutex);
        if(mode!=BridgeMode::Minecraft||!enabled||!updates.connected||(windowHandoff.epoch()&&windowHandoff.epoch()!=epoch))windowHandoff.cancel();
        else {
            windowPhase=windowHandoff.phase(windowState,epoch,GetTickCount64());
            const bool ack=updates.newMotion&&updates.motionValid&&updates.motionAgeMs<250&&updates.motion.epoch==epoch
                &&(updates.motion.flags&MotionNativeTraversalAck)&&!(updates.motion.flags&(MotionReady|MotionDead|MotionScreen))&&updates.motion.health>0;
            if(windowPhase==WindowHandoffPhase::AwaitLanding&&ack){
                const Vec3 feet{float(updates.motion.hostX*units),float(updates.motion.hostY*units*ySign),float(updates.motion.hostZ*units)};
                windowAcknowledged=windowHandoff.acknowledge(windowState,epoch,updates.motion.tick,feet,true,GetTickCount64());
            }
            windowFailed=windowHandoff.currentPhase()==WindowHandoffPhase::Failed;
        }
        windowPhase=windowHandoff.currentPhase();windowLanding=windowHandoff.landing();
        const auto previousScriptPhase=scriptHandoff.currentPhase();
        scriptPhase=scriptHandoff.observe(mode==BridgeMode::Minecraft&&enabled&&updates.connected,
            windowHandoff.blocksGuestMotion(),windowState,scriptState.cutscene,scriptState.canReplace(),epoch,
            updates.motion.tick,GetTickCount64(),units);
        const bool scriptAck=scriptPhase==WindowHandoffPhase::AwaitLanding&&scriptState.canReplace()
            &&updates.newMotion&&updates.motionValid&&updates.motionAgeMs<250&&updates.motion.epoch==epoch
            &&(updates.motion.flags&MotionNativeTraversalAck)&&!(updates.motion.flags&(MotionReady|MotionDead|MotionScreen))&&updates.motion.health>0;
        if(scriptAck){const Vec3 feet{float(updates.motion.hostX*units),float(updates.motion.hostY*units*ySign),float(updates.motion.hostZ*units)};
            scriptAcknowledged=scriptHandoff.acknowledge(windowState,epoch,updates.motion.tick,feet,true,GetTickCount64());}
        scriptPhase=scriptHandoff.currentPhase();scriptLanding=scriptHandoff.landing();
        scriptFailed=scriptPhase==WindowHandoffPhase::Failed;
        scriptBegan=previousScriptPhase!=scriptPhase&&scriptPhase==WindowHandoffPhase::NativeAction;
        scriptLanded=previousScriptPhase!=scriptPhase&&scriptPhase==WindowHandoffPhase::AwaitLanding;
        if(updates.newMotion&&updates.motionValid)windowContext.sourceFrame=updates.motion.tick;
        windowContext.mode=mode;windowContext.epoch=epoch;windowContext.units=units;
    }
    if(scriptBegan)logMessage("Minecraft native event: STA_EVENT owns scripted movement; source anchor retained.");
    if(scriptLanded)logMessage("Minecraft native event: actual native landing recorded; awaiting client/server source receipt.");
    if(windowAcknowledged||scriptAcknowledged){
        // The original action used its own camera. Require a fresh world draw
        // after it instead of inheriting the pre-action presentation deadline.
        worldSuccessTime=worldBootstrapTime=0;
        logMessage(scriptAcknowledged?"Minecraft native event: actual source landing acknowledged; guest movement may resume.":
            "Minecraft window traversal: actual source landing acknowledged; guest movement may resume.");
    }
    if(windowFailed||scriptFailed){
        cancelWindowHandoff();enabled=false;mode=BridgeMode::Native;++epoch;
        guest->restore();camera->restore();controls->restore();hostInput->setActive(false,false);
        worldDesired=false;worldSuccessTime=worldBootstrapTime=0;windowPhase=WindowHandoffPhase::Idle;
        scriptPhase=WindowHandoffPhase::Idle;
        logMessage(scriptFailed?"Minecraft native event failed its scoped landing receipt; restored native control.":
            "Minecraft window traversal failed its scoped landing receipt; restored native control.");
    }
    const bool windowOwnsMotion=windowPhase==WindowHandoffPhase::NativeAction||windowPhase==WindowHandoffPhase::AwaitLanding
        ||scriptPhase==WindowHandoffPhase::NativeAction||scriptPhase==WindowHandoffPhase::AwaitLanding;
    const bool windowNativeInput=windowOwnsMotion&&mode==BridgeMode::Minecraft&&foreground&&windowState.valid&&windowState.alive&&windowState.playing
        &&(windowPhase==WindowHandoffPhase::NativeAction||windowPhase==WindowHandoffPhase::AwaitLanding
            ||(scriptPhase==WindowHandoffPhase::AwaitLanding&&scriptState.canReplace()));
    const bool nativeLandingInput=windowNativeInput&&(windowPhase==WindowHandoffPhase::AwaitLanding||scriptPhase==WindowHandoffPhase::AwaitLanding);
    {std::lock_guard lock(windowMutex);guest->setNativeTraversalHold(windowHandoff.blocksGuestMotion()||scriptHandoff.blocksGuestMotion());}
    // The source closes the selector only after genuine adoption/cancellation.
    // ReplaceVehicle may reuse the same ID: a new epoch retires its old maps,
    // deformation endpoints, UI and collision authority before any new draw.
    if(!vehicleMode||!enabled||beamngMenuEpoch!=epoch){beamngMenuSeen=false;beamngMenuEpoch=epoch;}
    if(vehicleMode&&enabled&&updates.stateValid&&updates.motionValid&&updates.motion.epoch==epoch){
        const bool sourceMenu=(updates.state.flags&ScreenOpen)!=0;
        if(beamngMenuSeen&&!sourceMenu){
            ++epoch;beamngMenuEpoch=epoch;beamngMenuSeen=false;
            renderer->clear();hud->clear();carPresentation.clear();heldCarPresentation.clear();
            avatarEpoch=0xffffffff;collisionEpoch=0xffffffff;collisionComplete=false;
            worldSuccessTime=worldBootstrapTime=0;reportedMesh=false;reportedDiagnostics=false;
            logMessage("BeamNG original vehicle selector closed; renewing actual vehicle and terrain epoch.");
        }else if(sourceMenu)beamngMenuSeen=true;
    }
    renderer->retirePendingResources();
    const auto ingestStart=PerfClock::now();
    for(auto& tex:updates.textures){if(vehicleMode)renderer->uploadVehicleTexture(tex.id,tex.width,tex.height,std::move(tex.rgba),tex.format,tex.compressed);else renderer->uploadTexture(tex.id,tex.width,tex.height,std::move(tex.rgba),tex.format);}
    for(const auto& material:updates.vehicleMaterials)renderer->setVehicleMaterial(material.header,material.layers);
    if(updates.newMesh&&updates.meshHeader.epoch==epoch){
        std::vector<RenderBatch> batches;for(auto& b:updates.mesh)batches.push_back({b.textureId,b.flags,std::move(b.vertices),b.materialId,std::move(b.secondaryUV),std::move(b.sourceIndices),std::move(b.indices)});
        bool accepted=false;
        if(vehicleMode&&updates.surfaceMesh){const auto& q=updates.meshCapture.quaternion;
            const auto vehicleId=updates.vehicleValid&&updates.vehicle.epoch==epoch?updates.vehicle.vehicleId:0;
            accepted=renderer->setVehicleFrame(std::move(batches),{q[0],q[1],q[2],q[3]},updates.meshCapture.sourceSeconds,epoch,vehicleId,vehiclePresentationNowMs());
        }else accepted=renderer->setMesh(1,std::move(batches));
        if(accepted)avatarEpoch=epoch;
        renderer->setAvatarOrigin({float(updates.meshHeader.hostOriginX*units),float(updates.meshHeader.hostOriginY*units*ySign)+feetOffset,float(updates.meshHeader.hostOriginZ*units)},epoch);++frameCosts.avatarUpdates;
    }
    size_t queuedCarPoses=updates.vehiclePoses.size(),pushedCarPoses=0,rejectedCarPoses=0,skippedCarPoses=0;
    if(vehicleMode){
        for(const auto& received:updates.vehiclePoses){const auto& p=received.pose;if(p.epoch!=epoch){++skippedCarPoses;continue;}
            VehiclePresentationSample s{};s.epoch=p.epoch;s.vehicleId=p.vehicleId;s.tick=p.tick;s.sourceSeconds=p.sourceSeconds;
            s.position={p.hostX,p.hostY,p.hostZ};s.cameraPosition={p.cameraX,p.cameraY,p.cameraZ};
            s.orientation={p.quaternion[0],p.quaternion[1],p.quaternion[2],p.quaternion[3]};
            s.cameraOrientation={p.cameraQuaternion[0],p.cameraQuaternion[1],p.cameraQuaternion[2],p.cameraQuaternion[3]};
            s.fov=p.fov;s.cameraMode=p.cameraMode;
            if(carPresentation.push(s,received.arrivalMs))++pushedCarPoses;else ++rejectedCarPoses;
        }
    }
    if(minecraftMode&&updates.newScene&&updates.sceneHeader.epoch==epoch){std::vector<RenderBatch> batches;for(auto& b:updates.scene)batches.push_back({b.textureId,b.flags,std::move(b.vertices)});renderer->setMesh(2,std::move(batches));renderer->setSceneOrigin({float(updates.sceneHeader.hostOriginX*units),float(updates.sceneHeader.hostOriginY*units*ySign)+feetOffset,float(updates.sceneHeader.hostOriginZ*units)},epoch);}
    if(!vehicleMode&&updates.newEntities&&updates.entityHeader.epoch==epoch){std::vector<RenderBatch> batches;for(auto& b:updates.entities)batches.push_back({b.textureId,b.flags,std::move(b.vertices)});renderer->setMesh(3,std::move(batches));renderer->setDynamicOrigin({float(updates.entityHeader.hostOriginX*units),float(updates.entityHeader.hostOriginY*units*ySign)+feetOffset,float(updates.entityHeader.hostOriginZ*units)},epoch);}
    if(updates.newHud)hud->update(std::move(updates.hud));
    const double ingestCost=milliseconds(ingestStart);
    const bool wanted=enabled&&foreground&&native.canReplace();
    const ULONGLONG now=GetTickCount64();
    const bool motionFresh=updates.motionValid&&updates.motion.epoch==epoch&&(updates.motion.flags&MotionReady)&&(!(vehicleMode||sourceMode)||(updates.motion.flags&MotionCameraReady));
    // Car geometry carries its captured deformation, while actual physics
    // position arrives independently. Follow that fresh position without
    // waiting for another full mesh or inventing intermediate physics states.
    VehiclePresentationSample carView{};
    const bool sampledCarView=vehicleMode&&carPresentation.sample(vehiclePresentationNowMs(),carView);
    const bool carViewValid=vehicleMode&&updates.vehiclePoseValid&&updates.vehicleValid
        &&sampledCarView&&carView.epoch==epoch&&carView.vehicleId==updates.vehicle.vehicleId;
    if(vehicleMode&&updates.motionValid&&updates.motion.epoch==epoch&&updates.motionAgeMs<250&&avatarEpoch==epoch){
        if(carViewValid)renderer->setVehiclePose({float(carView.position.x*units),float(carView.position.y*units*ySign)+feetOffset,float(carView.position.z*units)},carView.orientation,epoch,carView.vehicleId,carView.sourceSeconds);
        else renderer->setAvatarOrigin({float(updates.motion.hostX*units),float(updates.motion.hostY*units*ySign)+feetOffset,float(updates.motion.hostZ*units)},epoch);
    }
    const bool vehicleTerrainReady=!vehicleMode||(collisionComplete&&collisionEpoch==epoch&&collisionAcceptedTime&&now-collisionAcceptedTime<2000&&updates.vehicleValid&&updates.vehicle.epoch==epoch&&updates.vehicle.flags==1);
    BeamngReadiness vehicleReadiness{};
    if(vehicleMode){
        vehicleReadiness.connected=updates.connected;vehicleReadiness.hostCanReplace=native.canReplace();vehicleReadiness.focused=foreground;
        vehicleReadiness.rendererReady=worldReady&&cameraReady&&controlsReady;vehicleReadiness.meshPresent=renderer->hasAvatar();
        vehicleReadiness.importedTerrain=vehicleTerrainReady;
        vehicleReadiness.stateAgeMs=updates.stateAgeMs;vehicleReadiness.meshAgeMs=updates.meshAgeMs;vehicleReadiness.motionAgeMs=updates.motionAgeMs;
        vehicleReadiness.terrainAgeMs=collisionAcceptedTime?now-collisionAcceptedTime:UINT64_MAX;vehicleReadiness.stateFlags=updates.state.flags;vehicleReadiness.meshEpoch=avatarEpoch;
        vehicleReadiness.motion=updates.motion;vehicleReadiness.terrain=capturedTerrain;vehicleReadiness.vehicleExtent=updates.vehicle.collisionRadius;
    }
    SourceGuestReadiness sourceReadiness{};
    if(sourceMode){
        sourceReadiness.mode=selectedBackend;sourceReadiness.connected=updates.connected;
        sourceReadiness.hostCanReplace=native.canReplace();sourceReadiness.focused=foreground;
        sourceReadiness.rendererReady=worldReady&&cameraReady&&controlsReady&&guestReady&&collisionReady;
        sourceReadiness.meshPresent=renderer->hasAvatar();sourceReadiness.completeTerrain=collisionComplete&&collisionEpoch==epoch;
        sourceReadiness.stateAgeMs=updates.stateAgeMs;sourceReadiness.meshAgeMs=updates.meshAgeMs;
        sourceReadiness.motionAgeMs=updates.motionAgeMs;sourceReadiness.terrainAgeMs=collisionAcceptedTime?now-collisionAcceptedTime:UINT64_MAX;
        sourceReadiness.stateFlags=updates.state.flags;sourceReadiness.meshEpoch=avatarEpoch;
        sourceReadiness.motion=updates.motion;sourceReadiness.terrain=capturedTerrain;
    }
    const bool sourcePhysicsReady=!sourceMode||sourceGuestReady(sourceReadiness,epoch);
    const bool previousWorldFresh=worldSuccessTime&&worldSuccessEpoch==epoch&&now-worldSuccessTime<=250;
    const bool sourceScreenOpen=(updates.state.flags&ScreenOpen)!=0;
    const bool uiViewFresh=updates.hudValid&&(!vehicleMode||beamngUiMatches(updates.hudValid,
        updates.hudAgeMs,updates.hudUiModeKnown,updates.hudUiScreenOpen,sourceScreenOpen));
    const bool vehicleMenuUI=vehicleMode&&wanted&&beamngMenuUIReady(vehicleReadiness,epoch,
        updates.stateValid&&updates.motionValid,uiViewFresh);
    // A delayed screenshot must not turn text typed in the original selector
    // into a fresh V toggle. This status grants no UI or vehicle authority.
    const bool vehicleSourceMenu=vehicleMode&&beamngSourceMenuOpen(vehicleReadiness,epoch,
        updates.stateValid&&updates.motionValid);
    const bool vehiclePhysicsReady=!vehicleMode||beamngReady(vehicleReadiness,epoch);
    if(!wanted)heldCarPresentation.clear();
    const bool vehicleVisualReady=!vehicleMode||(wanted&&heldCarPresentation.allow(vehicleReadiness,epoch,
        updates.vehicleValid&&updates.vehicle.epoch==epoch?updates.vehicle.vehicleId:0,carViewValid,previousWorldFresh,now,
        sourceScreenOpen,uiViewFresh));
    const bool visualReady=wanted&&worldReady&&cameraReady&&controlsReady&&updates.stateValid&&updates.meshValid&&avatarEpoch==epoch&&(updates.state.flags&InWorld)&&(!(updates.state.flags&Dead)||motionFresh)&&renderer->hasAvatar()&&(!sourceScreenOpen||uiViewFresh)&&vehicleVisualReady&&sourcePhysicsReady;
    // Preserve a fresh, already drawn actual car during a bounded source hold.
    // Physics authority still requires all original complete-terrain gates.
    if(!wanted||((vehicleMode||sourceMode)&&!visualReady))worldSuccessTime=worldBootstrapTime=0;
    if(visualReady&&!worldBootstrapTime)worldBootstrapTime=now;
    const bool worldFresh=worldSuccessTime&&worldSuccessEpoch==epoch&&now-worldSuccessTime<=250;
    // Camera bootstrap is bounded. Native movement/combat only transfer after
    // the real world pass has drawn successfully for this epoch.
    const bool worldAllowed=worldFresh||(!worldSuccessTime&&worldBootstrapTime&&now-worldBootstrapTime<=1000);
    {std::lock_guard lock(windowMutex);windowContext.requested=mode==BridgeMode::Minecraft&&wanted&&guestReady&&(motionFresh||windowOwnsMotion)&&!hostAuthority;windowContext.updatedAt=GetTickCount64();}
    const bool guestMovementRequested=!windowOwnsMotion&&wanted&&worldFresh&&(vehicleMode||sourceMode||!hostAuthority)&&guestReady&&motionFresh&&vehicleTerrainReady&&vehiclePhysicsReady&&sourcePhysicsReady;
    guest->setActive(guestMovementRequested,epoch,units,mode);
    {std::lock_guard lock(windowMutex);if(mode==BridgeMode::Minecraft)scriptHandoff.arm(guestMovementRequested,windowState,epoch);}
    if(updates.newMotion&&!(updates.motion.flags&MotionNativeTraversalAck))guest->updateMotion(updates.motion);
    if(minecraftGameplayRoutes(mode)&&updates.newBlocks)guest->setBlocks(updates.blockEpoch,updates.blocks);
    for(const auto& melee:updates.melee)guest->applyMelee(melee);
    // Retain RE4's actual scripted camera/model during the native action. The
    // source stays anchored and follows it, then draws again after landing ACK.
    bool active=visualReady&&worldAllowed&&!windowOwnsMotion;
    guest->setVehicleVisualHold(vehicleMode&&active&&!vehiclePhysicsReady,epoch);
    if(!active)guest->restore();
    const bool screenOpen=(active&&(updates.state.flags&ScreenOpen))||vehicleMenuUI;
    CameraSettings cameraSettings{};cameraSettings.active=active&&cameraReady;cameraSettings.mode=static_cast<CameraMode>(updates.state.cameraMode);
    cameraSettings.yawDegrees=-native.angles.y*(180.f/3.14159265358979323846f);cameraSettings.pitchDegrees=0;
    cameraSettings.worldUnitsPerBlock=units;cameraSettings.eyeHeightBlocks=(updates.state.flags&Crouching)?1.27f:1.62f;
    if(active&&motionFresh&&(updates.motion.flags&MotionCameraReady)){
        cameraSettings.guestCameraValid=true;cameraSettings.guestCameraPosition={float(updates.motion.cameraX*units),float(updates.motion.cameraY*units*ySign),float(updates.motion.cameraZ*units)};
        cameraSettings.guestYawDegrees=updates.motion.yaw;cameraSettings.guestPitchDegrees=updates.motion.pitch;cameraSettings.guestRollDegrees=updates.motion.cameraRoll;cameraSettings.fovDegrees=updates.motion.fov;
    }
    if(active&&carViewValid){
        const auto angles=vehicleCameraAngles(carView.cameraOrientation);
        cameraSettings.guestCameraValid=true;cameraSettings.mode=static_cast<CameraMode>(carView.cameraMode);
        cameraSettings.guestCameraPosition={float(carView.cameraPosition.x*units),float(carView.cameraPosition.y*units*ySign),float(carView.cameraPosition.z*units)};
        cameraSettings.guestYawDegrees=angles.yaw;cameraSettings.guestPitchDegrees=angles.pitch;cameraSettings.guestRollDegrees=angles.roll;cameraSettings.fovDegrees=carView.fov;
        cameraSettings.vehiclePresentationValid=true;cameraSettings.vehiclePresentation=carView;
    }
    camera->setState(cameraSettings);
    if(vehicleMode&&enabled){
        static uint32_t poseDiagnosticEpoch=UINT32_MAX;static unsigned poseDiagnosticRows=0;static ULONGLONG lastPoseDiagnostic=0;
        static uint64_t pushedTotal=0,rejectedTotal=0,skippedTotal=0;
        if(poseDiagnosticEpoch!=epoch){poseDiagnosticEpoch=epoch;poseDiagnosticRows=0;lastPoseDiagnostic=0;pushedTotal=rejectedTotal=skippedTotal=0;}
        pushedTotal+=pushedCarPoses;rejectedTotal+=rejectedCarPoses;skippedTotal+=skippedCarPoses;
        if(poseDiagnosticRows<32&&now-lastPoseDiagnostic>=250){
            bool appliedActive=false;const auto applied=camera->appliedVehicleDiagnostic(native,epoch,appliedActive);
            const auto& raw=updates.latestVehiclePose;
            char line[1000];sprintf_s(line,"Car pose pipeline: epoch=%u queued=%zu pushed/rejected/skipped=%llu/%llu/%llu accepted/ignored=%llu/%llu poseValid=%u age=%ums raw(epoch/id/tick/time)=%u/%u/%llu/%.6f metaValid=%u age=%ums meta(epoch/id/flags)=%u/%u/%u sampled=%u sample(epoch/id/tick/time)=%u/%u/%llu/%.6f carView=%u cameraRequested=%u cameraRecordable=%u cameraActive=%u appliedHistory=%zu visual=%u active=%u.",
                epoch,queuedCarPoses,pushedTotal,rejectedTotal,skippedTotal,updates.vehiclePoseAccepted,updates.vehiclePoseIgnored,unsigned(updates.vehiclePoseValid),updates.vehiclePoseAgeMs,raw.epoch,raw.vehicleId,raw.tick,raw.sourceSeconds,
                unsigned(updates.vehicleValid),updates.vehicleAgeMs,updates.vehicle.epoch,updates.vehicle.vehicleId,updates.vehicle.flags,unsigned(sampledCarView),carView.epoch,carView.vehicleId,carView.tick,carView.sourceSeconds,
                unsigned(carViewValid),unsigned(cameraSettings.vehiclePresentationValid),unsigned(cameraSettings.vehiclePresentationValid&&validVehiclePresentationSample(cameraSettings.vehiclePresentation)),unsigned(appliedActive),applied.total,unsigned(visualReady),unsigned(active));
            logMessage(line);++poseDiagnosticRows;lastPoseDiagnostic=now;
        }
    }
    controls->setActive((active||vehicleMenuUI||nativeLandingInput)&&controlsReady);controls->setScreenOpen(screenOpen);
    controls->setGuestAuthority(windowOwnsMotion||vehicleMode||((sourceMode||!hostAuthority)&&guestReady&&guest->active()));
    hostInput->attach(device,transport,camera);hostInput->setActive((active||vehicleMenuUI)&&!windowOwnsMotion,screenOpen&&!windowOwnsMotion);
    Input input{};input.selectedSlot=0xffffffff;
    if(wanted){input.flags=Enabled|Focused;if(hostAuthority&&minecraftMode)input.flags|=HostAuthority;else if(guestReady&&collisionReady)input.flags|=GuestAuthority;}
    if(native.paused||native.cutscene)input.flags|=Paused;
    // This exact window coroutine can temporarily pause keys. Keep only its
    // retained-anchor host position sync alive, never source physics/actions.
    if(windowNativeInput)input.flags=Enabled|Focused|HostAuthority|NativeTraversal;
    input.hostEpoch=epoch;
    input.hostX=native.position.x/units;input.hostY=native.position.y/(units*ySign);input.hostZ=native.position.z/units;
    if(windowNativeInput){const Vec3 target=scriptPhase==WindowHandoffPhase::AwaitLanding?scriptLanding.position:
            windowPhase==WindowHandoffPhase::AwaitLanding?windowLanding.position:windowState.position;
        input.hostX=target.x/units;input.hostY=target.y/(units*ySign);input.hostZ=target.z/units;}
    constexpr float degrees=180.f/3.14159265358979323846f;
    // RE4 yaw turns toward +X; Minecraft's positive yaw turns toward -X.
    auto look=camera->look();input.yaw=cameraReady&&active?look.yawDegrees:-native.angles.y*degrees;input.pitch=cameraReady&&active?look.pitchDegrees:0.f;
    if(active||vehicleMenuUI){
        const std::pair<int,uint32_t> held[]{{forwardKey,Forward},{backKey,Back},{leftKey,Left},{rightKey,Right},{VK_SPACE,Jump},{haloMode?haloCrouchKey:sneakKey,Sneak},{sourceMode?0:sprintKey,Sprint},{VK_LBUTTON,Attack},{VK_RBUTTON,Use}};
        for(auto [key,bit]:held)if(down(key)&&(!vehicleMode||!screenOpen||(bit&(Attack|Use))))input.held|=bit;
        if(!sourceMode)for(int key='1';key<='9';key++)if(down(key))input.selectedSlot=static_cast<uint32_t>(key-'1');
    }
    if(haloMode){
        // Halo's explicit v6 input contract: these bits mean original Halo
        // actions on its own peer, never Minecraft inventory/drop/chat.
        const std::pair<int,uint32_t> presses[]{{haloReloadKey,Drop},{haloGrenadeKey,SwapHands},{haloMeleeKey,Inventory},{haloUseKey,Chat},{haloWeaponKey,Command},{VK_ESCAPE,re4craft::Escape}};
        for(auto [key,bit]:presses)if(edge(key)&&active&&!screenOpen)input.pressed|=bit;
    }else if(!sourceMode){
        const std::pair<int,uint32_t> presses[]{{vehicleMode?beamngMenuKey:inventoryKey,Inventory},{vehicleMode?'R':dropKey,Drop},{'F',SwapHands},{VK_F5,Camera},{VK_ESCAPE,re4craft::Escape},{chatKey,Chat},{VK_OEM_2,Command}};
        for(auto [key,bit]:presses)if(edge(key)&&(active||vehicleMenuUI||(vehicleMode&&wanted&&
            ((bit&(Drop|Inventory))||(vehicleSourceMenu&&(bit&re4craft::Escape)))))&&
            (vehicleMode?(vehicleSourceMenu?(bit&re4craft::Escape):(bit&(Camera|Drop|Inventory))):!screenOpen))input.pressed|=bit;
    }
    if(edge('C')&&vehicleMode&&active&&!screenOpen)input.pressed|=Camera;
    if(vehicleMode&&(input.pressed&Drop))renderer->resetVehicleDeformation();
    hostInput->sample(input);
    if(windowNativeInput){input.held=0;input.pressed=0;input.scroll=0;input.mouseButtons=0;input.selectedSlot=UINT32_MAX;}
    transport->setInput(input);
    if(wanted&&collisionReady){
        Vec3 queryCenter=native.position;
        if((vehicleMode||sourceMode||!hostAuthority)&&updates.motionValid&&updates.motion.epoch==epoch){
            Vec3 feet{float(updates.motion.hostX*units),float(updates.motion.hostY*units*ySign),float(updates.motion.hostZ*units)};
            Vec3 delta{feet.x-native.position.x,feet.y-native.position.y,feet.z-native.position.z};
            // A same-epoch hold may leave the native proxy behind. Import at
            // the retained guest feet so Ready can recover, with the same
            // 32-block transfer bound as native motion. Ready is not granted
            // by this center; the returned complete terrain still decides it.
            if(std::isfinite(feet.x)&&std::isfinite(feet.y)&&std::isfinite(feet.z)&&delta.x*delta.x+delta.y*delta.y+delta.z*delta.z<=units*units*1024)queryCenter=feet;
        }
        float dx=queryCenter.x-collisionCenter.x,dy=queryCenter.y-collisionCenter.y,dz=queryCenter.z-collisionCenter.z;
        const float recenterDistance=units*std::max(8.f,terrainRadius*.25f);
        const bool recenter=collisionEpoch!=epoch||dx*dx+dy*dy+dz*dz>recenterDistance*recenterDistance;
        if((recenter||now-collisionTime>1000)&&now-collisionTime>250){
            // A large complete neighborhood retains a stable center while the
            // guest moves inside it. Refresh moving SAT instances every second,
            // and recenter with ample coverage left for an ordinary frame gap.
            CollisionReadOptions options{};options.center=recenter?queryCenter:collisionCenter;
            options.radius=terrainRadius*units;options.maxTriangles=200000;options.maxReadBytes=64u*1024u*1024u;
            const auto terrainStart=PerfClock::now();auto terrain=collision->capture(options);const double terrainCost=milliseconds(terrainStart);frameCosts.terrain+=terrainCost;frameCosts.terrainMax=std::max(frameCosts.terrainMax,terrainCost);collisionTime=now;
            if(terrain.valid){
                const bool movingSources=mode==BridgeMode::Minecraft;
                const uint32_t terrainFlags=terrain.complete&&!terrain.truncated?1u:terrain.truncated?2u:0u;
                Collision prefix{epoch,static_cast<uint32_t>(terrain.triangles.size()),terrainFlags|(movingSources?4u:0u),movingSources?++movingSurfaceRevision:0u,options.center.x/units,options.center.y/(units*ySign),options.center.z/units,options.radius/units};std::vector<uint8_t> packet(sizeof(prefix)+terrain.triangles.size()*(9*sizeof(float)+(movingSources?12:0)));std::memcpy(packet.data(),&prefix,sizeof(prefix));
                size_t at=sizeof(prefix);for(const auto& tri:terrain.triangles)for(const Vec3 point:{tri.a,tri.b,tri.c}){
                    float coords[]{point.x/units,point.y/(units*ySign),point.z/units};std::memcpy(packet.data()+at,coords,sizeof(coords));at+=sizeof(coords);
                }
                if(movingSources)for(const auto& tri:terrain.triangles){std::memcpy(packet.data()+at,&tri.sourceKey,8);std::memcpy(packet.data()+at+8,&tri.polygonIndex,4);at+=12;}
                if(transport->sendPacket(Message::Collision,std::move(packet))){
                    static uint32_t previousFlags=0xffffffff;
                    if(collisionEpoch!=epoch||previousFlags!=prefix.flags){char line[256];sprintf_s(line,"Exported RE4 SAT terrain: %u triangles, %u active entries radius=%.0f blocks complete=%u truncated=%u rejected=%u read=%.2f MiB",prefix.triangleCount,terrain.activeEntries,prefix.radius,unsigned(terrain.complete),unsigned(terrain.truncated),terrain.rejectedEntries,terrain.bytesRead/1048576.);logMessage(line);previousFlags=prefix.flags;}
                    collisionEpoch=epoch;collisionCenter=options.center;
                    collisionComplete=terrainFlags==1;
                    capturedTerrain=prefix;capturedTerrain.flags=terrainFlags;capturedTerrain.reserved=0;
                    movingSurfaceBaseline=movingSources?terrain.instances:std::vector<CollisionInstance>{};movingSurfaceTime=0;
                    collisionAcceptedTime=now;
                }
                else collisionComplete=false;
            }
            else collisionComplete=false;
        }
        if(mode==BridgeMode::Minecraft&&collisionEpoch==epoch&&now-movingSurfaceTime>=50){
            movingSurfaceTime=now;std::vector<CollisionTransform> transforms;
            if(CollisionExporter::captureTransforms(collision->satManager(),movingSurfaceBaseline,units,ySign,transforms)){
                MovingSurfaces prefix{epoch,movingSurfaceRevision,uint32_t(transforms.size()),0,now};
                std::vector<uint8_t> packet(sizeof(prefix)+transforms.size()*sizeof(MovingSurface));std::memcpy(packet.data(),&prefix,sizeof(prefix));size_t at=sizeof(prefix);
                for(const auto& transform:transforms){MovingSurface surface{};surface.sourceKey=transform.key;surface.flags=transform.flags;std::memcpy(surface.matrix,transform.matrix,sizeof(surface.matrix));std::memcpy(packet.data()+at,&surface,sizeof(surface));at+=sizeof(surface);}
                transport->sendPacket(Message::MovingSurfaces,std::move(packet));
            }
        }
    }
    for(const auto& damage:guest->takeDamage()){
        std::vector<uint8_t> packet(sizeof(damage));std::memcpy(packet.data(),&damage,sizeof(damage));
        if(!transport->sendPacket(Message::Damage,std::move(packet))){cancelWindowHandoff();++epoch;enabled=false;mode=BridgeMode::Native;active=false;guest->setLeonProtection(leonProtection.requested(),mode);guest->restore();controls->setGuestAuthority(false);logMessage("Host damage delivery failed; bridge disabled and native combat restored.");}
    }
    if(updates.connected&&minecraftGameplayRoutes(mode)){
        auto loot=guest->takeLoot();
        for(size_t index=0;index<loot.size();++index){
            std::vector<uint8_t> packet(sizeof(HostLoot));std::memcpy(packet.data(),&loot[index],sizeof(HostLoot));
            if(!transport->sendPacket(Message::Loot,std::move(packet))){
                bool retained=true;
                for(size_t retry=loot.size();retry>index;--retry)retained=guest->retryLoot(loot[retry-1])&&retained;
                logMessage(retained?"Native loot delivery retained for transport retry.":"Native loot retry queue exhausted; undelivered rewards require a new gameplay event.");
                break;
            }
        }
    }
    if(wanted&&guestReady&&GetTickCount64()-enemySendTime>=50){
        enemySendTime=GetTickCount64();auto enemies=guest->takeEnemies();EnemySnapshot header{epoch,static_cast<uint32_t>(enemies.size()),enemySendTime};
        std::vector<uint8_t> packet(sizeof(header)+enemies.size()*sizeof(Enemy));std::memcpy(packet.data(),&header,sizeof(header));if(!enemies.empty())std::memcpy(packet.data()+sizeof(header),enemies.data(),enemies.size()*sizeof(Enemy));transport->sendPacket(Message::Enemies,std::move(packet));
    }
    worldDesired=active;worldHeartbeat=GetTickCount64();
    worldPlacement={};worldPlacement.origin=native.position;worldPlacement.origin.y+=feetOffset;
        // Vanilla exporter has already posed and rotated geometry in Minecraft world space.
    worldPlacement.yawRadians=0.f;worldPlacement.worldUnitsPerBlock=units;worldPlacement.ySign=ySign;
    worldPlacement.drawScene=minecraftMode;worldPlacement.drawDynamic=!vehicleMode;worldPlacement.drawAvatar=!minecraftMode||updates.state.cameraMode!=0;worldPlacement.hostEpoch=epoch;
    bool show=active&&worldSuccessTime&&worldSuccessEpoch==epoch&&GetTickCount64()-worldSuccessTime<=250,overlayRendered=false;
    if(active||vehicleMenuUI){
        const auto hudStart=PerfClock::now();
        overlayRendered=updates.hudValid&&(!vehicleMode||beamngUiMatches(updates.hudValid,updates.hudAgeMs,updates.hudUiModeKnown,updates.hudUiScreenOpen,screenOpen))
            &&hud->render(device,screenOpen,input.cursorX,input.cursorY);
        const double hudCost=milliseconds(hudStart);frameCosts.hud+=hudCost;frameCosts.hudMax=std::max(frameCosts.hudMax,hudCost);
        if(!worldPlacement.drawAvatar)show=show&&overlayRendered;
        if(show&&!reportedMesh){if(auto* selected=backendFor(selectedBackend)){char line[256]{};sprintf_s(line,"Rendered actual %s source mesh; native model hidden only with fresh owned motion/camera/world readiness.",selected->name);logMessage(line);}reportedMesh=true;}
    }
    game->setReplacementVisible(show);
    controls->setHudHidden((show&&(overlayRendered||vehicleMode))||(vehicleMenuUI&&overlayRendered));
    if(!active&&!vehicleMenuUI){
        camera->restore();hostInput->setActive(false,false);
        // Keep the ordinary native walking/combat filter during the bounded
        // landing receipt. The original action itself retains native controls.
        if(!nativeLandingInput)controls->restore();
    }
    // Record the gate that actually restored Leon, without interpreting OS
    // Control as the unrelated native SPF_CTRL processing-stop bit.
    const uint32_t gates=(enabled?1u:0u)|(foreground?2u:0u)|(native.canReplace()?4u:0u)|(updates.connected?8u:0u)|(updates.stateValid?16u:0u)|(updates.meshValid?32u:0u)|(updates.hudValid?64u:0u)|(updates.motionValid?128u:0u)|(motionFresh?256u:0u)|(visualReady?512u:0u)|(worldFresh?1024u:0u)|(worldAllowed?2048u:0u)|(active?4096u:0u)|(guest->active()?8192u:0u);
    static uint32_t lastGates=0xffffffff,lastDiagnosticEpoch=0xffffffff;static ULONGLONG gateLogTime{};static unsigned gateLogs{};
    if((gates!=lastGates||epoch!=lastDiagnosticEpoch)&&now-gateLogTime>=250&&gateLogs<256){
        char line[640];sprintf_s(line,"Bridge activation changed: epoch=%u gates=%04X enabled=%u focused=%u native=%u active=%u guest=%u motion=%u/%02X epoch=%u ages(state/mesh/hud/motion)=%u/%u/%u/%u ms world=%u age=%llu ms stop=%08X status=%08X be=%08X routine=%u Ctrl=%u Space=%u.",epoch,gates,unsigned(enabled),unsigned(foreground),unsigned(native.canReplace()),unsigned(active),unsigned(guest->active()),unsigned(updates.motionValid),updates.motion.flags,updates.motion.epoch,updates.stateAgeMs,updates.meshAgeMs,updates.hudAgeMs,updates.motionAgeMs,unsigned(worldFresh),worldSuccessTime?now-worldSuccessTime:0,native.stopFlags,native.statusFlags,native.playerBeFlags,unsigned(native.routine),unsigned(down(VK_CONTROL)),unsigned(down(VK_SPACE)));logMessage(line);lastGates=gates;lastDiagnosticEpoch=epoch;gateLogTime=now;++gateLogs;
    }
    if(visualReady&&!worldAllowed&&!windowOwnsMotion){cancelWindowHandoff();++epoch;enabled=false;mode=BridgeMode::Native;guest->setLeonProtection(leonProtection.requested(),mode);worldDesired=false;guest->restore();camera->restore();controls->restore();hostInput->setActive(false,false);logMessage("World render watchdog expired; restored native control. F7/F8 can retry after checking the log.");}
    // Active includes the bounded camera bootstrap and valid empty world
    // scenes. Disabled/held bridges do not need per-object camera capture.
    setWorldRenderCaptureEnabled(worldDesired);
    if(wanted){
        if(!frameCosts.since)frameCosts.since=now;
        const double frameCost=milliseconds(perfStart);frameCosts.total+=frameCost;frameCosts.frameMax=std::max(frameCosts.frameMax,frameCost);frameCosts.ingest+=ingestCost;frameCosts.frames++;
        if(updates.meshValid){frameCosts.avatarAge+=updates.meshAgeMs;++frameCosts.avatarAgeSamples;frameCosts.avatarAgeMax=std::max(frameCosts.avatarAgeMax,updates.meshAgeMs);}
        if(now-frameCosts.since>=5000){char line[640];sprintf_s(line,"Bridge performance: EndScene=%.1f calls/s cost=%.2f ms HUD=%.2f ms ingest=%.2f ms terrain=%.2f ms world=%.2f ms avatarIngest=%.1f updates/s meshAge(avg/max)=%.1f/%u ms maxima(frame/HUD/terrain/world)=%.2f/%.2f/%.2f/%.2f ms.",frameCosts.frames*1000./double(now-frameCosts.since),frameCosts.total/frameCosts.frames,frameCosts.hud/frameCosts.frames,frameCosts.ingest/frameCosts.frames,frameCosts.terrain/frameCosts.frames,frameCosts.world/std::max(1u,frameCosts.worldFrames),frameCosts.avatarUpdates*1000./double(now-frameCosts.since),frameCosts.avatarAge/std::max(1u,frameCosts.avatarAgeSamples),frameCosts.avatarAgeMax,frameCosts.frameMax,frameCosts.hudMax,frameCosts.terrainMax,frameCosts.worldMax);logMessage(line);frameCosts={};}
    }else frameCosts={};
}
HRESULT WINAPI hookedEndScene(IDirect3DDevice9* device){
    if(!graphicsReady.load(std::memory_order_acquire))return originalEndScene(device);
    flushWorldRenderBeforeOverlay(device);
    {WorldRenderSuppression suppression;std::lock_guard lock(renderMutex);frame(device);}
    return originalEndScene(device);
}
HRESULT WINAPI hookedPresent(IDirect3DDevice9* device,const RECT* source,const RECT* dest,HWND window,const RGNDATA* dirty){
    if(!graphicsReady.load(std::memory_order_acquire))return originalPresent(device,source,dest,window,dirty);
    const auto start=PerfClock::now();HRESULT result=originalPresent(device,source,dest,window,dirty);const auto finished=PerfClock::now();
    std::lock_guard lock(renderMutex);
    if(device!=measuredDevice||FAILED(result))return result;
    const auto now=GetTickCount64();if(!presentCosts.since)presentCosts.since=now;
    if(presentCosts.last.time_since_epoch().count()){
        const double interval=std::chrono::duration<double,std::milli>(finished-presentCosts.last).count();
        presentCosts.intervals+=interval;presentCosts.smallest=std::min(presentCosts.smallest,interval);presentCosts.largest=std::max(presentCosts.largest,interval);++presentCosts.samples;
    }
    presentCosts.last=finished;++presentCosts.frames;if(enabled)++presentCosts.bridgeFrames;
    presentCosts.callTime+=std::chrono::duration<double,std::milli>(finished-start).count();
    if(now-presentCosts.since>=5000&&presentCosts.samples){
        char line[320];sprintf_s(line,"RE4 Present performance: %.1f FPS interval(avg/min/max)=%.2f/%.2f/%.2f ms Present=%.2f ms bridgeEnabled=%u/%u frames.",presentCosts.samples*1000./presentCosts.intervals,presentCosts.intervals/presentCosts.samples,presentCosts.smallest,presentCosts.largest,presentCosts.callTime/presentCosts.frames,presentCosts.bridgeFrames,presentCosts.frames);logMessage(line);presentCosts={};presentCosts.last=finished;presentCosts.since=now;
    }
    return result;
}
HRESULT WINAPI hookedReset(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* params){
    if(!graphicsReady.load(std::memory_order_acquire))return originalReset(device,params);
    {WorldRenderSuppression suppression;std::lock_guard lock(renderMutex);worldDesired=false;setWorldRenderCaptureEnabled(false);worldSuccessTime=worldBootstrapTime=0;presentCosts={};for(auto& backend:backends){if(backend.renderer)backend.renderer->onDeviceLost();if(backend.hud)backend.hud->onDeviceLost();}carPresentation.clear();heldCarPresentation.clear();if(camera)camera->restore();if(controls)controls->restore();if(guest)guest->restore();if(game)game->restore();}
    return originalReset(device,params);
}
bool installHooks(){
    // Use the native CreateDevice out-slot. A second HAL device can fail during
    // startup and can expose a different vtable from the one RE4 actually uses.
    hookdetail::ImageCode code;if(!code.initialize())return false;
    uintptr_t call=code.find("8B 35 ? ? ? ? 8B 0D ? ? ? ? 8B 11 68 ? ? ? ? 68 ? ? ? ? 6A ? 56 8B 35 ? ? ? ? 56 50 8B 42 40 51 FF D0 85 C0");
    uintptr_t factory=code.find("E8 ? ? ? ? A3 ? ? ? ? 85 C0 75 17");
    uintptr_t deviceSlot=call?hookdetail::operand(call+15):0,factorySlot=call?hookdetail::operand(call+8):0;
    uintptr_t presentation=call?hookdetail::operand(call+20):0;uint8_t behavior{};
    if(!call||!factory||!hookdetail::copy(call+25,&behavior,1)||(behavior!=0x44&&behavior!=0x40)
        ||!factorySlot||deviceSlot!=factorySlot+4||hookdetail::operand(factory+6)!=factorySlot
        ||!hookdetail::memory(deviceSlot,sizeof(uintptr_t),true)||!hookdetail::memory(presentation,sizeof(D3DPRESENT_PARAMETERS))){
        logMessage("Native D3D9 CreateDevice/factory signatures failed cross-check; graphics hooks disabled.");return false;
    }
    MH_STATUS initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){
        char line[160];sprintf_s(line,"D3D9 MinHook initialization failed: %s (%d).",MH_StatusToString(initialized),int(initialized));logMessage(line);return false;
    }
    struct HookOps {
        std::array<void*,3> targets{};
        std::array<void*,3> detours{reinterpret_cast<void*>(&hookedEndScene),reinterpret_cast<void*>(&hookedPresent),reinterpret_cast<void*>(&hookedReset)};
        std::array<void**,3> originals{reinterpret_cast<void**>(&originalEndScene),reinterpret_cast<void**>(&originalPresent),reinterpret_cast<void**>(&originalReset)};
        int create(size_t i){return MH_CreateHook(targets[i],detours[i],originals[i]);}
        int enable(size_t i){return MH_EnableHook(targets[i]);}
        int disable(size_t i){return MH_DisableHook(targets[i]);}
        int remove(size_t i){return MH_RemoveHook(targets[i]);}
    } ops;
    D3DHookTransaction<3> hooks;
    struct NativeReadOps {
        bool read(uintptr_t address,void* output,size_t size){return hookdetail::copy(address,output,size);}
        bool executable(void* address){MEMORY_BASIC_INFORMATION info{};
            if(!address||!VirtualQuery(address,&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
            DWORD protection=info.Protect&0xff;
            return protection==PAGE_EXECUTE||protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        }
    } reads;
    const ULONGLONG started=GetTickCount64();ULONGLONG reported{};
    logMessage("Waiting up to 30 seconds for RE4's native D3D9 device.");
    do {
        // Keep old target indices until every hook owned by an earlier attempt
        // is removed. Never disable another module's hooks or MH_ALL_HOOKS.
        auto cleaned=hooks.removeAll(ops);
        if(!cleaned.cleanupComplete){
            if(GetTickCount64()-reported>=2000){char line[192];sprintf_s(line,"D3D9 owned-hook cleanup retry: target=%u status=%s (%d).",unsigned(cleaned.index),MH_StatusToString(static_cast<MH_STATUS>(cleaned.status)),cleaned.status);logMessage(line);reported=GetTickCount64();}
            Sleep(250);continue;
        }
        D3DDeviceSnapshot snapshot{};
        if(!acquireD3DDeviceSnapshot(deviceSlot,reads,snapshot)){
            if(GetTickCount64()-reported>=2000){logMessage("Native D3D9 device not yet published with a readable table and executable methods; waiting.");reported=GetTickCount64();}
            Sleep(250);continue;
        }
        const auto& table=snapshot.table;
        ops.targets={table[42],table[17],table[16]};
        auto result=hooks.install(ops);
        if(!result.installed){
            if(GetTickCount64()-reported>=2000){char line[256];sprintf_s(line,"D3D9 hook retry: stage=%s target=%u status=%s (%d) cleanup=%u.",result.failedStage==HookStage::Create?"create":result.failedStage==HookStage::Enable?"enable":"remove",unsigned(result.index),MH_StatusToString(static_cast<MH_STATUS>(result.status)),result.status,unsigned(result.cleanupComplete));logMessage(line);reported=GetTickCount64();}
            Sleep(250);continue;
        }
        // Pass the validated snapshot so no borrowed COM object is reread after
        // the signature scans. All graphics calls stay on native callbacks.
        setWorldRenderCaptureEnabled(false);
        worldReady=installWorldRenderHook(game,table,drawWorld,logMessage);
        if(!worldReady){hooks.removeAll(ops);logMessage("D3D9 device hooks resolved, but world-depth setup failed; bridge remains disabled.");return false;}
        char line[256];sprintf_s(line,"Actual RE4 D3D9 device %08X table %08X: EndScene=%p Present=%p Reset=%p; graphics hooks installed.",unsigned(snapshot.device),unsigned(snapshot.tableAddress),table[42],table[17],table[16]);logMessage(line);
        return true;
    } while(GetTickCount64()-started<30000);
    auto cleaned=hooks.removeAll(ops);
    logMessage(cleaned.cleanupComplete?"Native D3D9 setup timed out; all hooks owned by the failed attempt were removed.":"Native D3D9 setup timed out; owned-hook cleanup remains incomplete and graphics callbacks stay inactive.");
    return false;
}
DWORD WINAPI initialize(void*){
    configure();logMessage("RE4Craft v0.2.0-alpha.2 protocol6: original UHD x86; compiled " __DATE__ " " __TIME__ "; starts disabled");
    game=new GameAccess();
    for(auto& backend:backends){const bool vehicle=backend.mode==BridgeMode::BeamNG;backend.renderer=new Renderer(vehicle);backend.transport=new Transport(logMessage,vehicle,vehicle);backend.hud=new HudRenderer();}
    auto* initial=backendFor(BridgeMode::Minecraft);renderer=initial->renderer;transport=initial->transport;hud=initial->hud;
    camera=new CameraControlBridge();controls=new HostControls();hostInput=new HostInput();collision=new CollisionExporter();guest=new GuestController();
    if(!game->initialize(logMessage)){logMessage("Unsupported executable signatures; no game changes made");return 0;}
    cameraReady=camera->installHooks(game,logMessage);controlsReady=controls->installHooks(game,logMessage);controls->setCamera(camera);
    collisionReady=collision->initialize(logMessage);
    guestReady=guest->installHooks(game,logMessage);
    if(guestReady&&!nativeGrabs.install([](void* context){return static_cast<GuestController*>(context)->preventNativeGrab();},guest,logMessage))
        logMessage("Common grab escape signatures unavailable; F2/guest automatic escape remains unavailable.");
    if(guestReady&&!nativeWindows.install(beginNativeWindow,completeNativeWindow,nullptr,logMessage))
        logMessage("Native window callback unavailable; contextual landing handoff is disabled.");
    if(!installHooks()){logMessage("D3D9 hook setup failed; no player replacement enabled");return 0;}
    for(auto& backend:backends)if(backendAvailable(backend.mode))backend.transport->start(backend.port);graphicsReady.store(true,std::memory_order_release);
    logMessage("D3D9 hooks installed. F8 Minecraft; F7 BeamNG preserved; Halo on hold, ULTRAKILL cancelled; F2 native Leon protection.");return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        pluginModule=instance;DisableThreadLibraryCalls(instance);
        wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
        const wchar_t* name=wcsrchr(path,L'\\');name=name?name+1:path;
        // Harness loads the same ASI to test codec without hooking unrelated executables.
        if(_wcsicmp(name,L"bio4.exe")==0){HANDLE worker=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);if(worker)CloseHandle(worker);}
    }
    return TRUE;
}
