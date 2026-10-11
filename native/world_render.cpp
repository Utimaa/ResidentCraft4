#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "world_render.hpp"
#include "world_phase.hpp"
#include "world_projection.hpp"
#include "world_camera.hpp"
#include "world_queue_record.hpp"
#include "hook_support.hpp"
#include "vendor/minhook/include/MinHook.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <mutex>
#include <chrono>

namespace re4craft { namespace {
using ProjectionFn=void(__cdecl*)(const uint8_t*);
using ProjectionQueueFn=void(__cdecl*)(const float*,uint32_t);
using BeginFn=HRESULT(WINAPI*)(IDirect3DDevice9*);
using ClearFn=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD);
using TargetFn=HRESULT(WINAPI*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
using DepthFn=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*);
using PrimitiveFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using IndexedFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using PrimitiveUpFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
using IndexedUpFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
ProjectionFn projectionOriginal{};BeginFn beginOriginal{};ClearFn clearOriginal{};TargetFn targetOriginal{};
DepthFn depthOriginal{};
ProjectionQueueFn projectionQueueOriginal{};
PrimitiveFn primitiveOriginal{};IndexedFn indexedOriginal{};PrimitiveUpFn primitiveUpOriginal{};IndexedUpFn indexedUpOriginal{};
GameAccess* access{};WorldDrawFn callback{};LogFn logger{};
std::vector<void*> installed;
std::atomic<uint64_t> attempts{},successes{};
uintptr_t rasterSlot{},executedProjectionSlot{},positionSlot{},queueSlot{},projectionConsumer{};
QueuedWorldCameras queuedCameras;std::mutex queueMutex;
std::atomic<uint64_t> queuedMain{},pairedMain{},missingMain{};
std::atomic<uint64_t> begunScenes{},armedBeginResets{};
std::atomic<uint64_t> producerCalls{},producerNanoseconds{},fullSamples{},cameraReads{},skippedConsumers{};
std::atomic<uint64_t> repeatedWorldProjections{};
std::atomic<bool> captureEnabled{true};
std::atomic<uint64_t> captureGeneration{};
thread_local WorldDepthPhase phase;
thread_local NativeSnapshot camera;
thread_local IDirect3DDevice9* activeDevice{};
thread_local bool drawingBridge{};
thread_local bool suppressed{};
thread_local uint64_t cameraQueued{};
thread_local float bestViewRotation{},bestViewTranslation{};
thread_local NativeSnapshot cachedEligibility;
thread_local uint64_t eligibilityTime{};
thread_local uint64_t eligibilityGeneration{};
struct QueueTiming{std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};uint64_t elapsed{};
    void pause(){elapsed+=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());}
    void resume(){start=std::chrono::steady_clock::now();}
    ~QueueTiming(){pause();producerNanoseconds+=elapsed;++producerCalls;}};
uintptr_t unique(const uint8_t* code,size_t length,const std::vector<int>& bytes) {
    uintptr_t match{};
    for(size_t i=0;i+bytes.size()<=length;++i) {
        bool equal=true;for(size_t n=0;n<bytes.size();++n)if(bytes[n]>=0&&code[i+n]!=bytes[n]){equal=false;break;}
        if(equal){if(match)return 0;match=reinterpret_cast<uintptr_t>(code+i);}
    }
    return match;
}
uintptr_t findProjectionConsumer() {
    auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if(!base||dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386)return 0;
    auto sections=IMAGE_FIRST_SECTION(nt);
    // Verified against the hash-pinned 1.1.0 executable: queued GX projection
    // record has matrix at +8 and projection type at +0x48; this consumer calls
    // real GXSetProjection, rather than appending another queue record.
    const std::vector<int> consumer{0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x48,0x48,0x51,0x83,0xc0,0x08,0x50,0xe8,-1,-1,-1,-1,0x83,0xc4,0x08,0x5d,0xc3};
    const std::vector<int> projection{0x55,0x8b,0xec,0x8b,0x45,0x0c,0x85,0xc0,0x75,-1,0xa3,-1,-1,-1,-1,0x8b,0x45,0x08,0xd9,0x00,0xd9,0x1d,-1,-1,-1,-1,0xd9,0x40,0x08};
    for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n)if(!std::memcmp(sections[n].Name,".text",5)) {
        auto code=base+sections[n].VirtualAddress;size_t length=sections[n].Misc.VirtualSize;
        uintptr_t realProjection=unique(code,length,projection);if(!realProjection)return 0;
        uintptr_t match{},start=reinterpret_cast<uintptr_t>(code),end=start+length;
        for(size_t i=0;i+consumer.size()<=length;++i) {
            bool equal=true;for(size_t b=0;b<consumer.size();++b)if(consumer[b]>=0&&code[i+b]!=consumer[b]){equal=false;break;}
            if(!equal)continue;
            int32_t relative{};std::memcpy(&relative,code+i+15,4);uintptr_t callee=start+i+19+relative;
            for(int branch=0;branch<4&&callee>=start&&callee+5<=end&&*reinterpret_cast<uint8_t*>(callee)==0xe9;++branch) {
                std::memcpy(&relative,reinterpret_cast<void*>(callee+1),4);callee+=5+relative;
            }
            // GXLoadTexMtxImm has the same consumer record shape. Select only
            // the consumer whose actual callee is the unique GXSetProjection.
            if(callee==realProjection){if(match)return 0;match=start+i;}
        }
        return match;
    }
    return 0;
}
bool matches(const uint8_t* command,const NativeSnapshot& native) {
    uint32_t kind{};std::memcpy(&kind,command+0x48,4);if(kind!=0||!native.canReplace())return false;
    float matrix[4][4]{};std::memcpy(matrix,command+8,sizeof(matrix));
    return sameWorldProjection(matrix,native);
}
uintptr_t findProjectionProducer(const hookdetail::ImageCode& code,uintptr_t consumer){
    // Queue record size0x4c, copied16 floats, exact callback pointer at+80.
    // GXLoadTexMtx has a similar shape; the consumer pairing distinguishes it.
    uintptr_t found{};
    const std::vector<int> pattern{0x55,0x8b,0xec,0x8b,0x0d,-1,-1,-1,-1,0x8b,0x81,0x04,0x00,0x30,0x00,0x83,0xc0,0x4c,0x3d,0x00,0x00,0x30,0x00,0x76,-1,0xe8,-1,-1,-1,-1,0x8b,0x0d,-1,-1,-1,-1,0x8b,0x81,0x04,0x00,0x30,0x00,0x03,0xc1,0x56,0x8b,0x75,0x08,0xba,0x4c,0x00,0x00,0x00};
    for(size_t i=0;i+96<=code.size;++i){bool equal=true;for(size_t n=0;n<pattern.size();++n)if(pattern[n]>=0&&code.begin[i+n]!=pattern[n]){equal=false;break;}
        if(equal){uintptr_t address=reinterpret_cast<uintptr_t>(code.begin+i);
            if(hookdetail::operand(address+80)!=consumer||hookdetail::operand(address+5)!=hookdetail::operand(address+32))continue;
            if(found)return 0;found=address;}}
    return found;
}
void __cdecl projectionQueueHook(const float* matrix,uint32_t kind){
    if(!captureEnabled.load(std::memory_order_relaxed)){projectionQueueOriginal(matrix,kind);return;}
    QueueTiming timing;
    const uint64_t producerGeneration=captureGeneration.load(std::memory_order_relaxed);
    NativeSnapshot sampled{};float copied[4][4]{};
    bool capture=kind==0&&access&&hookdetail::copy(reinterpret_cast<uintptr_t>(matrix),copied,sizeof(copied));
    if(capture){const uint64_t now=GetTickCount64();sampled=cachedEligibility;
        // Eligibility/room/player metadata changes at the native frame rate.
        // Read the exact camera every candidate, but share the full native
        // snapshot across repeated object projections for at most16ms.
        bool refresh=!sampled.valid||now-eligibilityTime>=16||producerGeneration!=eligibilityGeneration;
        if(!refresh){++cameraReads;refresh=!access->readCamera(sampled)||sampled.globalAddress!=cachedEligibility.globalAddress;}
        if(refresh){sampled=access->sample();cachedEligibility=sampled;eligibilityTime=now;eligibilityGeneration=producerGeneration;++fullSamples;}
        capture=sampled.canReplace()&&sameWorldProjection(copied,sampled);}
    timing.pause();projectionQueueOriginal(matrix,kind);timing.resume();
    if(!capture)return;
    uintptr_t queue=hookdetail::operand(queueSlot);uint32_t offset=hookdetail::operand(queue+0x300004);
    if(offset<0x4c||offset>0x300000)return;
    uintptr_t command=queue+offset-0x4c;
    if(!readQueuedProjection(command,projectionConsumer,kind,copied))return;
    std::lock_guard<std::mutex> lock(queueMutex);queuedCameras.store(command,GetTickCount64(),copied,sampled,producerGeneration);++queuedMain;
}
void flush(const char* reason) {
    const uint32_t nativeDraws=phase.nativeDraws;
    if(drawingBridge||suppressed||!phase.boundary()||!activeDevice||!callback)return;
    uint64_t attempt=++attempts;drawingBridge=true;
    // Read the exact raster matrix after the actual native draws, rather than
    // inferring zoom from a back-buffer aspect or sampling a later HUD phase.
    NativeSnapshot rendered=camera;float raster[4][4]{},executed[4][4]{};
    bool exactRaster=rasterSlot&&executedProjectionSlot&&hookdetail::copy(rasterSlot,raster,sizeof(raster))&&hookdetail::copy(executedProjectionSlot,executed,sizeof(executed))&&composeExecutedRasterProjection(rendered,raster,executed);
    bool okay=exactRaster&&callback(activeDevice,rendered);
    drawingBridge=false;if(okay)++successes;
    static bool previousResult{};static unsigned transitionLogs{};
    bool changed=okay!=previousResult;previousResult=okay;
    if(logger&&(attempt<=12||attempt%120==0||(changed&&transitionLogs++<48))) {
        IDirect3DSurface9 *target{},*depth{};D3DSURFACE_DESC rt{},ds{};D3DVIEWPORT9 vp{};
        activeDevice->GetRenderTarget(0,&target);activeDevice->GetDepthStencilSurface(&depth);activeDevice->GetViewport(&vp);
        if(target)target->GetDesc(&rt);if(depth)depth->GetDesc(&ds);
        auto current=access?access->sample():NativeSnapshot{};double cameraShift=std::sqrt(std::pow(double(current.cameraPosition.x)-camera.cameraPosition.x,2)+std::pow(double(current.cameraPosition.y)-camera.cameraPosition.y,2)+std::pow(double(current.cameraPosition.z)-camera.cameraPosition.z,2));
        char message[1024]{};std::snprintf(message,sizeof(message),"World-depth pass: boundary=%s attempt=%llu success=%u raster=%u diag=(%.4f,%.4f,%.4f,%.4f) RT=%p %ux%u DS=%p %ux%u VP=%ux%u queueAge=%llums cameraShift=%.3f viewRotation=%.6f viewTranslation=%.3f paired=%llu/%llu missing=%llu begun=%llu armedBeginResets=%llu executedZ=(%.9f,%.6f) queueHookAvg=%.2fus fullSamples=%llu cameraReads=%llu skippedConsumers=%llu repeatedWorld=%llu nativeDraws=%u.",reason,attempt,unsigned(okay),unsigned(exactRaster),raster[0][0],raster[1][1],raster[2][2],raster[3][3],static_cast<void*>(target),rt.Width,rt.Height,static_cast<void*>(depth),ds.Width,ds.Height,vp.Width,vp.Height,GetTickCount64()-cameraQueued,cameraShift,bestViewRotation,bestViewTranslation,pairedMain.load(),queuedMain.load(),missingMain.load(),begunScenes.load(),armedBeginResets.load(),executed[2][2],executed[3][2],producerCalls?double(producerNanoseconds.load())/producerCalls.load()/1000:0,fullSamples.load(),cameraReads.load(),skippedConsumers.load(),repeatedWorldProjections.load(),nativeDraws);
        logger(message);if(target)target->Release();if(depth)depth->Release();
    }
    if(logger){
        // Window deltas expose dense-view cost without a lifetime average
        // hiding the view currently causing dropped frames.
        static uint64_t since{},previousCalls{},previousTime{},previousReads{},previousFull{},previousBegun{};
        const uint64_t now=GetTickCount64(),calls=producerCalls.load(),time=producerNanoseconds.load();
        const uint64_t reads=cameraReads.load(),full=fullSamples.load(),begun=begunScenes.load();
        if(!since){since=now;previousCalls=calls;previousTime=time;previousReads=reads;previousFull=full;previousBegun=begun;}
        if(now-since>=5000){
            const uint64_t count=calls-previousCalls,elapsed=time-previousTime,scenes=begun-previousBegun;
            char message[384]{};std::snprintf(message,sizeof(message),"World queue performance: %.1f calls/s hook=%.2f us total=%.2f ms/s cameraReads=%llu fullSamples=%llu scenes=%llu hookPerScene=%.3f ms.",count*1000./double(now-since),count?double(elapsed)/count/1000:0,double(elapsed)/double(now-since)/1000,reads-previousReads,full-previousFull,scenes,scenes?double(elapsed)/scenes/1e6:0);
            logger(message);since=now;previousCalls=calls;previousTime=time;previousReads=reads;previousFull=full;previousBegun=begun;
        }
    }
}
void __cdecl projectionHook(const uint8_t* command) {
    if(!captureEnabled.load(std::memory_order_relaxed)){projectionOriginal(command);phase.arm(false);return;}
    // The phase can deliver at most once per actual GPU scene. Repeated
    // object projections after that boundary need no camera lookup or sample.
    if(phase.delivered){projectionOriginal(command);phase.arm(false);++skippedConsumers;return;}
    if(access&&activeDevice&&!drawingBridge&&!suppressed) {NativeSnapshot sampled{};float matrix[4][4]{};uint64_t queued{};bool paired=false;
        if(hookdetail::copy(reinterpret_cast<uintptr_t>(command)+8,matrix,sizeof(matrix))){std::lock_guard<std::mutex> lock(queueMutex);paired=queuedCameras.take(reinterpret_cast<uintptr_t>(command),matrix,sampled,queued,captureGeneration.load(std::memory_order_relaxed));}
        bool main=paired&&matches(command,sampled);
        // Native objects repeatedly enqueue the same world projection. Such a
        // reassertion is not the end of world drawing: flushing there lets
        // subsequent native geometry overwrite non-depth-writing skin/water.
        bool continuation=phase.continues(main&&sameQueuedWorldView(camera,sampled));
        if(!continuation)flush("projection-transition");
        projectionOriginal(command);
        if(continuation){cameraQueued=queued;++repeatedWorldProjections;return;}
        if(main){camera=sampled;cameraQueued=queued;bestViewRotation=bestViewTranslation=1e9f;++pairedMain;}
        else {uint32_t kind{};std::memcpy(&kind,command+0x48,4);if(kind==0)++missingMain;}
        phase.arm(main); }
    else projectionOriginal(command);
}
HRESULT WINAPI beginHook(IDirect3DDevice9* d) {HRESULT result=beginOriginal(d);if(SUCCEEDED(result)){if(d==activeDevice&&phase.perspective&&!phase.delivered)++armedBeginResets;++begunScenes;activeDevice=d;phase.begin();}return result;}
HRESULT WINAPI clearHook(IDirect3DDevice9* d,DWORD count,const D3DRECT* rect,DWORD flags,D3DCOLOR color,float depth,DWORD stencil) {
    if(d==activeDevice&&(flags&(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER)))flush("clear");
    return clearOriginal(d,count,rect,flags,color,depth,stencil);
}
HRESULT WINAPI targetHook(IDirect3DDevice9* d,DWORD slot,IDirect3DSurface9* target) {
    if(d==activeDevice&&slot==0){IDirect3DSurface9* current{};d->GetRenderTarget(0,&current);bool changed=current!=target;if(current)current->Release();if(changed)flush("target");}
    return targetOriginal(d,slot,target);
}
HRESULT WINAPI depthHook(IDirect3DDevice9* d,IDirect3DSurface9* next) {
    const bool enabled=captureEnabled.load(std::memory_order_relaxed);
    if(enabled&&d==activeDevice&&!drawingBridge&&!suppressed){
        IDirect3DSurface9* current{};
        const HRESULT observed=d->GetDepthStencilSurface(&current);
        const bool changed=worldDepthBindingBoundary(enabled,d==activeDevice,drawingBridge,suppressed,
                                                     SUCCEEDED(observed),current,next);
        if(current)current->Release();
        // RE4 may detach its world depth before switching render targets.
        // Submit while that genuine attachment is still bound; never retain
        // or reattach an old surface after the native transition.
        if(changed)flush("depth");
    }
    return depthOriginal(d,next);
}
void note(IDirect3DDevice9* d,HRESULT result,UINT count){if(d==activeDevice&&!drawingBridge&&!suppressed&&SUCCEEDED(result)&&count){phase.nativeDraw();
    // A native selected position matrix can also contain a model transform.
    // Record nearest view agreement as evidence; never treat an arbitrary
    // model/bone matrix as the world camera.
    unsigned draws=phase.nativeDraws;if(phase.perspective&&draws&&(draws<=16||(draws&(draws-1))==0)){float columns[4][4]{};
        if(positionSlot&&hookdetail::copy(positionSlot,columns,sizeof(columns))){float rotation{},translation{};
            for(int r=0;r<3;++r){for(int c=0;c<3;++c)rotation=(std::max)(rotation,std::abs(columns[c][r]-camera.view[r][c]));translation=(std::max)(translation,std::abs(columns[3][r]-camera.view[r][3]));}
            if(rotation<bestViewRotation-.00001f||(std::abs(rotation-bestViewRotation)<.00001f&&translation<bestViewTranslation)){bestViewRotation=rotation;bestViewTranslation=translation;}}}}}
HRESULT WINAPI primitiveHook(IDirect3DDevice9* d,D3DPRIMITIVETYPE kind,UINT first,UINT count){HRESULT r=primitiveOriginal(d,kind,first,count);note(d,r,count);return r;}
HRESULT WINAPI indexedHook(IDirect3DDevice9* d,D3DPRIMITIVETYPE kind,INT base,UINT min,UINT vertices,UINT first,UINT count){HRESULT r=indexedOriginal(d,kind,base,min,vertices,first,count);note(d,r,count);return r;}
HRESULT WINAPI primitiveUpHook(IDirect3DDevice9* d,D3DPRIMITIVETYPE kind,UINT count,const void* data,UINT stride){HRESULT r=primitiveUpOriginal(d,kind,count,data,stride);note(d,r,count);return r;}
HRESULT WINAPI indexedUpHook(IDirect3DDevice9* d,D3DPRIMITIVETYPE kind,UINT min,UINT vertices,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride){HRESULT r=indexedUpOriginal(d,kind,min,vertices,count,indices,format,data,stride);note(d,r,count);return r;}
bool add(void* target,void* detour,void** original){if(MH_CreateHook(target,detour,original)!=MH_OK)return false;installed.push_back(target);return MH_EnableHook(target)==MH_OK;}
}
bool installWorldRenderHook(GameAccess* game,const std::array<void*,85>& table,WorldDrawFn draw,LogFn log) {
    if(!game||!game->initialized()||!draw||!installed.empty())return false;
    for(size_t index:{size_t(41),size_t(43),size_t(37),size_t(39),size_t(81),size_t(82),size_t(83),size_t(84)})if(!table[index])return false;
    auto projection=findProjectionConsumer();if(!projection){if(log)log("World-depth projection consumer signature missing or nonunique; world rendering disabled.");return false;}
    hookdetail::ImageCode code;if(!code.initialize())return false;
    projectionConsumer=projection;uintptr_t producer=findProjectionProducer(code,projection);queueSlot=producer?hookdetail::operand(producer+5):0;
    if(!producer||!queueSlot||!hookdetail::memory(queueSlot,4)){if(log)log("Queued world-camera producer signature missing/nonunique; world rendering disabled.");return false;}
    // Exact final post-projection multiplication in the verified GX MVP
    // builder. The two operands are columns2 and0 of the same contiguous
    // matrix; the signature/cross-check rejects an unrelated multiply.
    uintptr_t raster=code.find("0F 59 35 ? ? ? ? 0F C6 E4 00 0F 59 25 ? ? ? ? 0F 58 F4 0F 58 DE");
    rasterSlot=raster?hookdetail::operand(raster+14):0;
    if(!rasterSlot||hookdetail::operand(raster+3)!=rasterSlot+32||!hookdetail::memory(rasterSlot,64)){if(log)log("Executed GX raster-matrix signature missing/nonunique; world rendering disabled.");rasterSlot=0;return false;}
    uintptr_t executed=code.find("D9 5D 0C D9 00 D9 1D ? ? ? ? D9 40 10 D9 1D ? ? ? ? D9 40 20 D9 1D ? ? ? ? D9 40 30 D9 1D ? ? ? ?");
    executedProjectionSlot=executed?hookdetail::operand(executed+7):0;
    uintptr_t mvp=code.find("0F 28 0D ? ? ? ? 0F 28 2D ? ? ? ? 0F 28 35 ? ? ? ? 0F 28 25 ? ? ? ? 0F 28 C1");positionSlot=mvp?hookdetail::operand(mvp+10)-48:0;
    if(!executedProjectionSlot||hookdetail::operand(executed+16)!=executedProjectionSlot+4||!hookdetail::memory(executedProjectionSlot,64)||!positionSlot||hookdetail::operand(mvp+17)!=positionSlot+16||hookdetail::operand(mvp+24)!=positionSlot+32||!hookdetail::memory(positionSlot,64)){if(log)log("Executed GX projection/position matrix signature missing; world rendering disabled.");return false;}
    access=game;callback=draw;logger=log;
    bool okay=add(reinterpret_cast<void*>(producer),reinterpret_cast<void*>(projectionQueueHook),reinterpret_cast<void**>(&projectionQueueOriginal))
        &&add(reinterpret_cast<void*>(projection),reinterpret_cast<void*>(projectionHook),reinterpret_cast<void**>(&projectionOriginal))
        &&add(table[41],reinterpret_cast<void*>(beginHook),reinterpret_cast<void**>(&beginOriginal))
        &&add(table[43],reinterpret_cast<void*>(clearHook),reinterpret_cast<void**>(&clearOriginal))
        &&add(table[37],reinterpret_cast<void*>(targetHook),reinterpret_cast<void**>(&targetOriginal))
        &&add(table[39],reinterpret_cast<void*>(depthHook),reinterpret_cast<void**>(&depthOriginal))
        &&add(table[81],reinterpret_cast<void*>(primitiveHook),reinterpret_cast<void**>(&primitiveOriginal))
        &&add(table[82],reinterpret_cast<void*>(indexedHook),reinterpret_cast<void**>(&indexedOriginal))
        &&add(table[83],reinterpret_cast<void*>(primitiveUpHook),reinterpret_cast<void**>(&primitiveUpOriginal))
        &&add(table[84],reinterpret_cast<void*>(indexedUpHook),reinterpret_cast<void**>(&indexedUpOriginal));
    if(!okay){removeWorldRenderHook();if(log)log("World-depth hooks could not be installed; native model remains available.");return false;}
    if(log){char message[320]{};std::snprintf(message,sizeof(message),"Queued GX projection consumer %08X paired with camera producer %08X; raster %08X executed projection %08X selected position %08X; world depth boundary hooks installed.",static_cast<unsigned>(projection),static_cast<unsigned>(producer),static_cast<unsigned>(rasterSlot),static_cast<unsigned>(executedProjectionSlot),static_cast<unsigned>(positionSlot));log(message);}return true;
}
void removeWorldRenderHook(){for(auto target:installed){MH_DisableHook(target);MH_RemoveHook(target);}installed.clear();access=nullptr;callback=nullptr;logger=nullptr;rasterSlot=executedProjectionSlot=positionSlot=queueSlot=projectionConsumer=0;}
uint64_t worldRenderAttempts(){return attempts.load();}
uint64_t worldRenderSuccesses(){return successes.load();}
bool setWorldRenderSuppressed(bool value){bool old=suppressed;suppressed=value;if(value){phase.perspective=false;phase.nativeDraws=0;}return old;}
void flushWorldRenderBeforeOverlay(IDirect3DDevice9* device){if(captureEnabled.load(std::memory_order_relaxed)&&device==activeDevice)flush("world-EndScene");}
void setWorldRenderCaptureEnabled(bool enabled){if(captureEnabled.exchange(enabled)!=enabled)++captureGeneration;}
}
