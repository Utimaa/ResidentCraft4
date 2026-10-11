#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include "../native/renderer.hpp"
#include "../native/hud_renderer.hpp"
#include "../native/world_phase.hpp"
#include "../native/world_projection.hpp"
#include "../native/world_camera.hpp"
#include "../native/world_queue_record.hpp"
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr UINT Width = 320, Height = 240;
void check(bool okay, const char* operation) { if (!okay) throw std::runtime_error(operation); }
void hr(HRESULT value, const char* operation) {
    if (FAILED(value)) { char message[256]; std::snprintf(message,sizeof(message),"%s failed 0x%08lx",operation,static_cast<unsigned long>(value)); throw std::runtime_error(message); }
}
struct Image { std::vector<uint32_t> pixels; unsigned visible{}, red{}; };
Image readback(IDirect3DDevice9* device) {
    IDirect3DSurface9 *target{}, *system{};
    hr(device->GetRenderTarget(0,&target),"GetRenderTarget");
    D3DSURFACE_DESC description{};
    hr(target->GetDesc(&description),"GetDesc");
    hr(device->CreateOffscreenPlainSurface(description.Width,description.Height,description.Format,D3DPOOL_SYSTEMMEM,&system,nullptr),"CreateOffscreenPlainSurface");
    HRESULT result=device->GetRenderTargetData(target,system); target->Release();
    hr(result,"GetRenderTargetData");
    D3DLOCKED_RECT locked{};
    hr(system->LockRect(&locked,nullptr,D3DLOCK_READONLY),"readback LockRect");
    Image image; image.pixels.resize(Width*Height);
    for(UINT y=0;y<Height;++y) {
        const auto row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(locked.pBits)+y*locked.Pitch);
        for(UINT x=0;x<Width;++x) {
            uint32_t pixel=row[x]; image.pixels[y*Width+x]=pixel;
            if((pixel&0xffffff)!=0) ++image.visible;
            if(((pixel>>16)&255)>180 && ((pixel>>8)&255)<20 && (pixel&255)<20) ++image.red;
        }
    }
    system->UnlockRect();system->Release();return image;
}
void writeBmp(const Image& image,const char* path) {
    BITMAPFILEHEADER file{}; file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+Width*Height*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=Width;info.biHeight=-static_cast<LONG>(Height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=Width*Height*4;
    std::ofstream stream(path,std::ios::binary);check(static_cast<bool>(stream),"open output BMP");
    stream.write(reinterpret_cast<const char*>(&file),sizeof(file));stream.write(reinterpret_cast<const char*>(&info),sizeof(info));stream.write(reinterpret_cast<const char*>(image.pixels.data()),image.pixels.size()*4);
    check(static_cast<bool>(stream),"write output BMP");
}
re4craft::NativeSnapshot camera(bool avatar=false) {
    re4craft::NativeSnapshot native{};native.valid=native.alive=native.playing=true;
    native.view[0][0]=native.view[1][1]=native.view[2][2]=1;
    if(avatar) native.view[1][3]=-0.9f;
    native.projection[0][0]=2.41421356f/(float(Width)/Height);native.projection[1][1]=2.41421356f;
    native.projection[2][2]=-1.f/99;native.projection[2][3]=-100.f/99;native.projection[3][2]=-1;
    return native;
}
re4craft::NativeSnapshot measuredCamera() {
    // Read-only snapshot from the user's loaded RE4 1.1.0 room. The old 100
    // units/block default put an entire 1.8m Minecraft body below this camera.
    auto native=camera();native.position={-93606.f,-117.2818298f,-2973.4597168f};
    const float view[3][4]{{0.6593649983f,-1.7888345e-8f,0.7518230081f,63678.1992188f},{0.1777286530f,0.9716565609f,-0.1558718383f,14742.8837891f},{-0.7305137515f,0.2363969088f,0.6406763196f,-68037.0703125f}};
    const float projection[4][4]{{1.6083809137f,0,0,0},{0,2.1445078850f,0,0},{0,0,-0.0001000100f,-100.0100021362f},{0,0,-1,0}};
    std::memcpy(native.view,view,sizeof(view));std::memcpy(native.projection,projection,sizeof(projection));return native;
}
void draw(re4craft::Renderer& renderer,IDirect3DDevice9* device,const re4craft::NativeSnapshot& native,float depth,bool expectedDraw=true,const re4craft::RenderPlacement* overridePlacement=nullptr) {
    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,depth,0),"Clear");
    hr(device->BeginScene(),"BeginScene");
    re4craft::RenderPlacement placement{};placement.origin={0,0,-4};placement.worldUnitsPerBlock=1;
    if(overridePlacement)placement=*overridePlacement;
    bool drawn=renderer.render(device,native,placement);
    hr(device->EndScene(),"EndScene");
    check(drawn==expectedDraw,"unexpected Renderer::render result");
}
Image repeatedProjectionOrdering(re4craft::Renderer& renderer,IDirect3DDevice9* device,bool retainWorld,re4craft::RenderPlacement placement,const re4craft::NativeSnapshot& renderingCamera=camera(true)) {
    using namespace re4craft;
    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"reassert world clear");hr(device->BeginScene(),"reassert BeginScene");
    struct ScreenVertex{float x,y,z,rhw;uint32_t color;};
    const ScreenVertex background[6]{{0,0,.999f,1,0xff000000},{float(Width),0,.999f,1,0xff000000},{float(Width),float(Height),.999f,1,0xff000000},{0,0,.999f,1,0xff000000},{float(Width),float(Height),.999f,1,0xff000000},{0,float(Height),.999f,1,0xff000000}};
    auto nativeBackground=[&](){hr(device->SetVertexShader(nullptr),"reassert native VS");hr(device->SetPixelShader(nullptr),"reassert native PS");hr(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"reassert native FVF");
        device->SetTexture(0,nullptr);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
        hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,background,sizeof(ScreenVertex)),"reassert later native world geometry");};
    WorldDepthPhase phase;phase.begin();phase.arm(true);nativeBackground();phase.nativeDraw();
    // The old consumer flushed here solely because another object reasserted
    // the identical world camera, before native world geometry was complete.
    if(!retainWorld){check(phase.boundary(),"old reassert boundary did not fire");check(renderer.render(device,renderingCamera,placement),"old capture submission");}
    else check(phase.continues(true)&&phase.nativeDraws==1,"exact camera reassert discarded pending native world");
    nativeBackground();phase.nativeDraw();
    if(retainWorld){check(phase.nativeDraws==2&&phase.boundary(),"actual camera transition missed complete native world");check(renderer.render(device,renderingCamera,placement),"complete-world capture submission");}
    hr(device->EndScene(),"reassert EndScene");return readback(device);
}
Image depthDetachOrdering(re4craft::Renderer& renderer,IDirect3DDevice9* device) {
    using namespace re4craft;
    IDirect3DSurface9 *depth{},*target{},*laterTarget{};
    hr(device->GetDepthStencilSurface(&depth),"detach fixture actual native depth");
    hr(device->GetRenderTarget(0,&target),"detach fixture native target");
    check(depth&&target,"detach fixture missing actual surfaces");
    struct Restore {IDirect3DDevice9* device;IDirect3DSurface9 *depth,*target,*later;
        ~Restore(){device->SetRenderTarget(0,target);device->SetDepthStencilSurface(depth);
            if(later)later->Release();if(target)target->Release();if(depth)depth->Release();}}
        restore{device,depth,target,nullptr};
    hr(device->CreateRenderTarget(Width,Height,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&laterTarget,nullptr),"detach fixture later target");restore.later=laterTarget;
    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"detach fixture clear");
    hr(device->BeginScene(),"detach fixture BeginScene");
    struct ScreenVertex {float x,y,z,rhw;uint32_t color;};
    const ScreenVertex wall[6]{{0,0,.1f,1,0xff0000ff},{Width/2.f,0,.1f,1,0xff0000ff},{Width/2.f,float(Height),.1f,1,0xff0000ff},
        {0,0,.1f,1,0xff0000ff},{Width/2.f,float(Height),.1f,1,0xff0000ff},{0,float(Height),.1f,1,0xff0000ff}};
    hr(device->SetVertexShader(nullptr),"detach wall VS");hr(device->SetPixelShader(nullptr),"detach wall PS");hr(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"detach wall FVF");
    device->SetTexture(0,nullptr);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    WorldDepthPhase phase;phase.begin();phase.arm(true);
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,wall,sizeof(ScreenVertex)),"detach fixture foreground native occluder");phase.nativeDraw();
    // The production hook predicate must not consume this pending world on
    // repeated bindings, dormant capture, foreign devices or recursive draws.
    check(!worldDepthBindingBoundary(true,true,false,false,true,depth,depth),"same real depth prematurely flushed");
    hr(device->SetDepthStencilSurface(depth),"detach fixture same-depth native binding");
    check(!worldDepthBindingBoundary(false,true,false,false,true,depth,nullptr),"disabled depth capture flushed");
    check(!worldDepthBindingBoundary(true,false,false,false,true,depth,nullptr),"foreign device depth flushed");
    check(!worldDepthBindingBoundary(true,true,true,false,true,depth,nullptr),"recursive bridge depth flushed");
    check(!worldDepthBindingBoundary(true,true,false,true,true,depth,nullptr),"suppressed bridge depth flushed");
    check(!worldDepthBindingBoundary(true,true,false,false,false,depth,nullptr),"failed depth query supplied evidence");
    check(!worldDepthBindingBoundary(true,true,false,false,true,nullptr,laterTarget),"missing depth supplied world evidence");
    check(phase.continues(true)&&phase.nativeDraws==1&&!phase.delivered,"guarded depth boundary discarded actual native draws");
    check(worldDepthBindingBoundary(true,true,false,false,true,depth,nullptr)&&phase.boundary(),"real depth detach did not close world before unbinding");
    RenderPlacement placement{};placement.origin={0,0,-4};placement.worldUnitsPerBlock=1;
    bool worldValid=false;check(renderer.render(device,camera(),placement,&worldValid)&&worldValid,"before-detach world submission rejected");
    IDirect3DSurface9* stillBound{};hr(device->GetDepthStencilSurface(&stillBound),"verify still-bound native depth");
    const bool preserved=stillBound==depth;if(stillBound)stillBound->Release();check(preserved,"bridge altered genuine native depth binding");
    hr(device->SetDepthStencilSurface(nullptr),"native world depth detach");
    worldValid=true;check(!renderer.render(device,camera(),placement,&worldValid)&&!worldValid,"late missing depth incorrectly accepted or reattached");
    check(!phase.boundary(),"target transition repeated an already delivered depth boundary");
    hr(device->SetRenderTarget(0,laterTarget),"native target transition after depth detach");
    hr(device->EndScene(),"detach fixture EndScene");
    hr(device->SetRenderTarget(0,target),"restore detach fixture target");hr(device->SetDepthStencilSurface(depth),"restore detach fixture depth");
    return readback(device);
}
struct CaptureBounds {re4craft::Vec3 min{},max{};bool valid{},translucent{},crumbling{};};
bool loadCapture(re4craft::Renderer& renderer,const char* path,uint32_t category=1,CaptureBounds* bounds=nullptr,std::vector<re4craft::RenderBatch>* baseGeometry=nullptr) {
    using namespace re4craft;
    std::ifstream input(path,std::ios::binary);check(static_cast<bool>(input),"open capture");
    bool gotTexture=false,gotMesh=false;
    while(true) {
        Header header{};input.read(reinterpret_cast<char*>(&header),sizeof(header));
        if(input.eof()&&input.gcount()==0)break;
        check(input.gcount()==sizeof(header)&&header.magic==Magic&&header.version==ProtocolVersion&&header.size<=MaxPayload,"invalid capture header");
        std::vector<uint8_t> payload(header.size);input.read(reinterpret_cast<char*>(payload.data()),payload.size());check(static_cast<bool>(input),"truncated capture");
        if(header.type==Message::Texture) {
            check(payload.size()>=sizeof(Texture),"capture texture prefix");Texture texture{};std::memcpy(&texture,payload.data(),sizeof(texture));
            check(renderer.uploadTexture(texture.id,texture.width,texture.height,payload.data()+sizeof(texture),payload.size()-sizeof(texture)),"capture texture rejected");gotTexture=true;
        } else if(header.type==Message::Mesh) {
            check(payload.size()>=sizeof(Mesh),"capture mesh prefix");Mesh mesh{};std::memcpy(&mesh,payload.data(),sizeof(mesh));check(mesh.category>=1&&mesh.category<=3&&mesh.batchCount<=MaxBatches,"capture category/batches");
            if(mesh.category!=category)continue;
            size_t offset=sizeof(mesh);std::vector<RenderBatch> batches;bool translucent=false,crumbling=false;
            for(uint32_t n=0;n<mesh.batchCount;++n) {
                check(offset<=payload.size()&&sizeof(Batch)<=payload.size()-offset,"capture batch prefix");Batch batch{};std::memcpy(&batch,payload.data()+offset,sizeof(batch));offset+=sizeof(batch);
                check(batch.vertexCount<=MaxVertices&&size_t(batch.vertexCount)*sizeof(Vertex)<=payload.size()-offset,"capture vertex count");
                translucent|=batch.vertexCount&&(batch.flags&2);
                crumbling|=batch.vertexCount&&(batch.flags&4);
                RenderBatch out{batch.textureId,batch.flags,{}};out.vertices.resize(batch.vertexCount);
                std::memcpy(out.vertices.data(),payload.data()+offset,out.vertices.size()*sizeof(Vertex));offset+=out.vertices.size()*sizeof(Vertex);batches.push_back(std::move(out));
            }
            if(baseGeometry){*baseGeometry=batches;for(auto& b:*baseGeometry){b.textureId=0;b.flags=0;for(auto& v:b.vertices)v.color=0xffb0b0b0;}}
            check(offset==payload.size()&&renderer.setMesh(mesh.category,std::move(batches)),"capture mesh rejected");gotMesh=true;
            if(bounds){*bounds={};size_t vertexOffset=sizeof(mesh);for(uint32_t n=0;n<mesh.batchCount;++n){Batch batch{};std::memcpy(&batch,payload.data()+vertexOffset,sizeof(batch));vertexOffset+=sizeof(batch);for(uint32_t i=0;i<batch.vertexCount;++i){Vertex v{};std::memcpy(&v,payload.data()+vertexOffset,sizeof(v));vertexOffset+=sizeof(v);if(!bounds->valid){bounds->min=bounds->max={v.x,v.y,v.z};bounds->valid=true;}else{bounds->min.x=(std::min)(bounds->min.x,v.x);bounds->min.y=(std::min)(bounds->min.y,v.y);bounds->min.z=(std::min)(bounds->min.z,v.z);bounds->max.x=(std::max)(bounds->max.x,v.x);bounds->max.y=(std::max)(bounds->max.y,v.y);bounds->max.z=(std::max)(bounds->max.z,v.z);}}}}
            if(bounds)bounds->translucent=translucent;
            if(bounds)bounds->crumbling=crumbling;
        }
    }
    return gotTexture&&gotMesh&&(category!=1||renderer.hasAvatar());
}
}

int main(int argc,char** argv) {
    IDirect3D9* api{};IDirect3DDevice9* device{};HWND window{};
    try {
        WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="RE4CraftRenderHarness";
        check(RegisterClassA(&wc)!=0,"RegisterClass");
        window=CreateWindowExA(0,wc.lpszClassName,"RE4Craft renderer verification",WS_OVERLAPPEDWINDOW,0,0,Width,Height,nullptr,nullptr,wc.hInstance,nullptr);
        check(window!=nullptr,"CreateWindow");
        api=Direct3DCreate9(D3D_SDK_VERSION);check(api!=nullptr,"Direct3DCreate9");
        D3DPRESENT_PARAMETERS parameters{};parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;parameters.hDeviceWindow=window;parameters.BackBufferWidth=Width;parameters.BackBufferHeight=Height;parameters.BackBufferFormat=D3DFMT_X8R8G8B8;parameters.EnableAutoDepthStencil=TRUE;parameters.AutoDepthStencilFormat=D3DFMT_D24S8;
        hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&parameters,&device),"CreateDevice HAL");
        {
            using namespace re4craft;
            Renderer renderer;
            const uint8_t rgba[16]{255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
            check(renderer.uploadTexture(7,2,2,rgba,sizeof(rgba)),"valid texture rejected");
            check(!renderer.uploadTexture(0,2,2,rgba,sizeof(rgba)),"invalid texture ID accepted");
            check(!renderer.uploadTexture(8,2049,2,rgba,sizeof(rgba)),"oversized texture accepted");
            check(!renderer.uploadTexture(8,2,2,rgba,sizeof(rgba)-1),"truncated texture accepted");
            RenderBatch triangle{7,0,{{-1,-1,0,0,0,1,0,1,0xffffffff},{1,-1,0,0,0,1,1,1,0xffffffff},{0,1,0,0,0,1,0.5f,0,0xffffffff}}};
            check(renderer.setMesh(1,{triangle})&&renderer.hasAvatar(),"valid triangle rejected");
            auto bad=triangle;bad.vertices[0].x=std::numeric_limits<float>::quiet_NaN();check(!renderer.setMesh(1,{bad})&&renderer.hasAvatar(),"non-finite mesh was accepted or replaced the valid mesh");
            bad=triangle;bad.vertices.pop_back();check(!renderer.setMesh(1,{bad}),"non-triangle mesh accepted");
            check(!renderer.setMesh(99,{triangle}),"invalid category accepted");
            D3DMATRIX sentinel{};for(int i=0;i<4;++i)sentinel.m[i][i]=1;sentinel.m[3][0]=7;
            hr(device->SetTransform(D3DTS_WORLD,&sentinel),"sentinel world");hr(device->SetTransform(D3DTS_VIEW,&sentinel),"sentinel view");hr(device->SetTransform(D3DTS_PROJECTION,&sentinel),"sentinel projection");
            hr(device->SetRenderState(D3DRS_LIGHTING,TRUE),"sentinel lighting");hr(device->SetRenderState(D3DRS_ZENABLE,D3DZB_FALSE),"sentinel depth");hr(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW),"sentinel cull");hr(device->SetRenderState(D3DRS_ALPHAREF,127),"sentinel alpha");hr(device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR),"sentinel sampler");hr(device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE),"sentinel FVF");
            D3DVIEWPORT9 expectedViewport{};hr(device->GetViewport(&expectedViewport),"sentinel viewport");
            IDirect3DTexture9* expectedTexture{};hr(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&expectedTexture,nullptr),"sentinel texture");hr(device->SetTexture(0,expectedTexture),"sentinel bound texture");
            IDirect3DVertexBuffer9* expectedStream{};hr(device->CreateVertexBuffer(64,0,D3DFVF_XYZ|D3DFVF_DIFFUSE,D3DPOOL_MANAGED,&expectedStream,nullptr),"sentinel stream");hr(device->SetStreamSource(0,expectedStream,0,16),"sentinel bound stream");
            draw(renderer,device,camera(),1);
            auto image=readback(device);check(image.red>500,"textured mesh produced no red pixels");
            writeBmp(image,"build\\tests\\renderer-triangle.bmp");
            for(auto state:{D3DRS_LIGHTING,D3DRS_ZENABLE,D3DRS_CULLMODE,D3DRS_ALPHAREF}) {
                DWORD expected=state==D3DRS_LIGHTING?TRUE:state==D3DRS_ZENABLE?D3DZB_FALSE:state==D3DRS_CULLMODE?D3DCULL_CW:127,value{};
                hr(device->GetRenderState(state,&value),"GetRenderState");check(value==expected,"renderer leaked a render state");
            }
            DWORD value{};hr(device->GetSamplerState(0,D3DSAMP_MAGFILTER,&value),"GetSamplerState");check(value==D3DTEXF_LINEAR,"renderer leaked sampler state");
            hr(device->GetFVF(&value),"GetFVF");check(value==(D3DFVF_XYZ|D3DFVF_DIFFUSE),"renderer leaked FVF");
            D3DVIEWPORT9 restoredViewport{};hr(device->GetViewport(&restoredViewport),"GetViewport");check(std::memcmp(&expectedViewport,&restoredViewport,sizeof(expectedViewport))==0,"renderer leaked viewport");
            IDirect3DBaseTexture9* restoredTexture{};hr(device->GetTexture(0,&restoredTexture),"GetTexture");check(restoredTexture==expectedTexture,"renderer leaked bound texture");if(restoredTexture)restoredTexture->Release();expectedTexture->Release();
            IDirect3DVertexBuffer9* restoredStream{};UINT restoredOffset{},restoredStride{};hr(device->GetStreamSource(0,&restoredStream,&restoredOffset,&restoredStride),"GetStreamSource");check(restoredStream==expectedStream&&restoredOffset==0&&restoredStride==16,"renderer leaked vertex stream");if(restoredStream)restoredStream->Release();expectedStream->Release();
            for(auto transform:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION}) { D3DMATRIX restored{};hr(device->GetTransform(transform,&restored),"GetTransform");check(std::memcmp(&restored,&sentinel,sizeof(sentinel))==0,"renderer leaked camera matrix"); }
            draw(renderer,device,camera(),0);check(readback(device).visible==0,"avatar ignored the host depth buffer");
            // Reproduce the live scheduling defect: a wall exists in world
            // depth, but UI clears that depth before EndScene. Draw at the
            // production boundary decision and retain its wall occlusion.
            struct ScreenVertex{float x,y,z,rhw;uint32_t color;};
            const ScreenVertex wall[6]{{0,0,.1f,1,0xff0000ff},{Width/2.f,0,.1f,1,0xff0000ff},{Width/2.f,float(Height),.1f,1,0xff0000ff},{0,0,.1f,1,0xff0000ff},{Width/2.f,float(Height),.1f,1,0xff0000ff},{0,float(Height),.1f,1,0xff0000ff}};
            auto renderOrdering=[&](bool early){
                hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"ordering world clear");hr(device->BeginScene(),"ordering BeginScene");
                hr(device->SetVertexShader(nullptr),"wall shader");hr(device->SetPixelShader(nullptr),"wall pixel shader");hr(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"wall FVF");
                device->SetTexture(0,nullptr);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
                device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
                WorldDepthPhase world;world.begin();world.arm(true);
                hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,wall,sizeof(ScreenVertex)),"native wall geometry");world.nativeDraw();
                RenderPlacement placement{};placement.origin={0,0,-4};placement.worldUnitsPerBlock=1;
                check(world.boundary(),"world boundary failed to flush native draws");
                if(early)check(renderer.render(device,camera(),placement),"early world geometry rejected");
                hr(device->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0),"host UI depth clear");
                if(!early)check(renderer.render(device,camera(),placement),"late reproduction rejected");
                check(!world.boundary(),"world draw repeated at later UI boundary");hr(device->EndScene(),"ordering EndScene");return readback(device);
            };
            auto early=renderOrdering(true),late=renderOrdering(false);size_t occludedPixel=(Height/2)*Width+Width/2-20;
            check((early.pixels[occludedPixel]&0xffffff)==0xff,"world boundary lost native wall occlusion");
            check((late.pixels[occludedPixel]&0xffffff)==0xff0000&&late.red>early.red+100,"ordering regression did not reproduce EndScene depth-clear defect");
            WorldDepthPhase empty;empty.begin();empty.arm(false);empty.nativeDraw();check(!empty.boundary(),"non-main perspective rendered bridge");empty.arm(true);check(!empty.boundary(),"empty main perspective rendered bridge");empty.begin();empty.arm(true);empty.nativeDraw();check(empty.boundary(),"new GPU scene did not reset boundary");
            writeBmp(early,"build\\tests\\renderer-world-boundary.bmp");std::printf("PASS: retained world depth before UI clear (%u red pixels), late EndScene defect reproduced (%u).\n",early.red,late.red);
            for(bool translucent:{false,true}){
                auto detachMesh=triangle;
                if(translucent){detachMesh.flags=2;detachMesh.textureId=0;for(auto& v:detachMesh.vertices)v.color=0x80ff0000;}
                check(renderer.setMesh(1,{detachMesh}),"depth detach bridge fixture mesh");
                auto detached=depthDetachOrdering(renderer,device);
                const size_t frontPixel=(Height/2)*Width+Width/2-20,visiblePixel=(Height/2)*Width+Width/2+20;
                check((detached.pixels[frontPixel]&0xffffff)==0xff,"depth detach boundary lost actual foreground occlusion");
                const uint32_t visible=detached.pixels[visiblePixel];
                check(((visible>>16)&255)>60&&(visible&255)<20,"depth detach boundary lost visible opaque/translucent bridge geometry");
                writeBmp(detached,translucent?"build\\tests\\renderer-world-depth-detach-transparent.bmp":"build\\tests\\renderer-world-depth-detach-opaque.bmp");
            }
            check(renderer.setMesh(1,{triangle}),"restore depth detach fixture mesh");
            std::puts("PASS: opaque/transparent world composition precedes real depth detach and target switch; same-depth, disabled, recursion and missing-depth gates preserve authority.");
            auto translucentWorld=triangle;translucentWorld.flags=2;check(renderer.setMesh(1,{translucentWorld}),"reassert translucent fixture");RenderPlacement reassertPlacement{};reassertPlacement.origin={0,0,-4};reassertPlacement.worldUnitsPerBlock=1;
            auto premature=repeatedProjectionOrdering(renderer,device,false,reassertPlacement),complete=repeatedProjectionOrdering(renderer,device,true,reassertPlacement);
            check(premature.visible==0&&complete.visible>500,"identical projection reassert did not reproduce/fix later-native overwrite");
            WorldDepthPhase continuation;continuation.begin();continuation.arm(true);continuation.nativeDraw();check(!continuation.continues(false)&&continuation.boundary(),"actual camera change did not end world phase");
            check(renderer.setMesh(1,{triangle}),"restore reassert fixture");std::puts("PASS: repeated exact world projection retains pending native draws; later-native overwrite of translucent geometry is reproduced and prevented.");
            // Draw the nearer surface first: without depth writes a later rear
            // face paints through Steve's head/body, reproducing the live defect.
            auto front=triangle;front.textureId=0;for(auto& v:front.vertices){v.color=0xffff0000;v.z=.5f;}
            auto rear=triangle;rear.textureId=0;for(auto& v:rear.vertices){v.color=0xff0000ff;v.z=-.5f;}
            check(renderer.setMesh(1,{front,rear}),"self-occlusion fixture rejected");draw(renderer,device,camera(),1);image=readback(device);
            check((image.pixels[(Height/2)*Width+Width/2]&0xffffff)==0xff0000,"rear face painted through nearer opaque surface");
            check(renderer.setMesh(1,{rear,front}),"reversed self-occlusion fixture rejected");draw(renderer,device,camera(),1);check((readback(device).pixels[(Height/2)*Width+Width/2]&0xffffff)==0xff0000,"opaque self-occlusion depends on packet order");
            auto transparentFront=front;transparentFront.flags=2;for(auto& v:transparentFront.vertices)v.color=0x80ff0000;
            auto transparentRear=rear;transparentRear.flags=2;for(auto& v:transparentRear.vertices)v.color=0x800000ff;
            check(renderer.setMesh(1,{transparentFront,transparentRear}),"translucent fixture rejected");draw(renderer,device,camera(),1);uint32_t blended=readback(device).pixels[(Height/2)*Width+Width/2];
            check(((blended>>16)&255)>(blended&255),"translucent triangles were not sorted back-to-front across batches");
            auto invisible=front;for(auto& v:invisible.vertices)v.color=0x00ff0000;check(renderer.setMesh(1,{invisible,rear}),"alpha-cutout fixture rejected");draw(renderer,device,camera(),1);check((readback(device).pixels[(Height/2)*Width+Width/2]&0xffffff)==0xff,"transparent cutout incorrectly wrote depth");
            RenderBatch square{0,1,{{-1,-1,0,0,0,1,0,0,0xffff0000},{1,-1,0,0,0,1,0,0,0xffff0000},{1,1,0,0,0,1,0,0,0xffff0000},{-1,-1,0,0,0,1,0,0,0xffff0000},{1,1,0,0,0,1,0,0,0xffff0000},{-1,1,0,0,0,1,0,0,0xffff0000}}};
            check(renderer.setMesh(1,{square}),"aspect fixture rejected");D3DVIEWPORT9 widescreen{0,0,Width,180,0,1};hr(device->SetViewport(&widescreen),"widescreen viewport");draw(renderer,device,camera(),1);image=readback(device);
            unsigned minX=Width,minY=Height,maxX=0,maxY=0;for(UINT y=0;y<180;++y)for(UINT x=0;x<Width;++x)if((image.pixels[y*Width+x]&0xffffff)==0xff0000){minX=(std::min)(minX,x);maxX=(std::max)(maxX,x);minY=(std::min)(minY,y);maxY=(std::max)(maxY,y);}
            check(maxX>minX&&maxY>minY&&std::abs(int(maxX-minX)-int(maxY-minY))<=2,"4:3 native projection stretched exported model in 16:9 viewport");hr(device->SetViewport(&expectedViewport),"restore harness viewport");
            check(renderer.setMesh(2,{triangle}),"scene-only mesh rejected");RenderPlacement scenePlacement{};scenePlacement.origin={0,0,-4};scenePlacement.worldUnitsPerBlock=1;scenePlacement.drawScene=true;scenePlacement.drawAvatar=false;draw(renderer,device,camera(),1,true,&scenePlacement);check(readback(device).red>500,"first-person scene-only draw produced no geometry");
            check(renderer.setMesh(1,{triangle}),"restore harness avatar");std::puts("PASS: opaque self-occlusion independent of batch order, sorted translucency, alpha holes, 16:9 model proportions and first-person scene-only draw.");
            auto anchoredScene=triangle;anchoredScene.textureId=0;for(auto& v:anchoredScene.vertices)v.color=0xff0000ff;
            check(renderer.setMesh(2,{anchoredScene}),"anchored scene fixture rejected");renderer.setAvatarOrigin({.8f,0,-4},11);renderer.setSceneOrigin({-.8f,0,-4},11);
            RenderPlacement anchoredPlacement{};anchoredPlacement.origin={70,0,5};anchoredPlacement.worldUnitsPerBlock=1;anchoredPlacement.yawRadians=1;anchoredPlacement.drawScene=true;
            draw(renderer,device,camera(),1,true,&anchoredPlacement);auto anchoredImage=readback(device);anchoredPlacement.origin={-70,20,-90};anchoredPlacement.yawRadians=-2;
            draw(renderer,device,camera(),1,true,&anchoredPlacement);check(readback(device).pixels==anchoredImage.pixels,"moving native feet dragged independent capture origins");check(anchoredImage.red>500,"independent avatar capture origin missing");
            unsigned blue{};for(auto pixel:anchoredImage.pixels)if((pixel&0xffffff)==0xff)++blue;check(blue>500,"independent scene capture origin missing");anchoredPlacement.hostEpoch=12;draw(renderer,device,camera(),1,false,&anchoredPlacement);check(readback(device).visible==0,"stale room epoch avatar was rendered");anchoredPlacement.hostEpoch=11;draw(renderer,device,camera(),1,true,&anchoredPlacement);check(readback(device).pixels==anchoredImage.pixels,"matching room epoch changed anchored geometry");renderer.setAvatarOrigin({},0);renderer.setSceneOrigin({},0);
            std::puts("PASS: independent avatar/scene capture origins remain fixed across native movement and yaw changes.");
            auto dropped=triangle;dropped.textureId=0;for(auto& v:dropped.vertices)v.color=0xff00ff00;
            check(renderer.setMesh(3,{dropped}),"dynamic item/projectile category rejected");renderer.setDynamicOrigin({0,0,-4},33);
            RenderPlacement dynamicPlacement{};dynamicPlacement.origin={90,90,90};dynamicPlacement.worldUnitsPerBlock=1;dynamicPlacement.drawAvatar=false;dynamicPlacement.drawScene=false;dynamicPlacement.hostEpoch=33;
            draw(renderer,device,camera(),1,true,&dynamicPlacement);image=readback(device);check((image.pixels[(Height/2)*Width+Width/2]&0xffffff)==0x00ff00,"first-person dynamic-only mesh produced no pixels");
            dynamicPlacement.hostEpoch=34;draw(renderer,device,camera(),1,false,&dynamicPlacement);check(readback(device).visible==0,"stale room dynamic item was rendered");
            check(renderer.setMesh(3,{}),"empty dynamic pickup/removal frame rejected");dynamicPlacement.hostEpoch=33;draw(renderer,device,camera(),1,false,&dynamicPlacement);renderer.setDynamicOrigin({},0);
            std::puts("PASS: category3 item/projectile meshes render in first person, reject stale epochs, and clear on pickup/removal.");
            // Compare the bridge with an independent native fixed-function
            // draw using the projection/raster values measured in live RE4.
            // The old aspect correction preserved proportions but was zoomed
            // out25%, so it could not register with actual host wall depth.
            auto rasterCamera=camera();const float gx[4][4]{{1.0711109638f,0,0,0},{0,1.4281480312f,0,0},{0,0,-.0001000100019f,-100.0100021362f},{0,0,-1,0}};
            std::memcpy(rasterCamera.projection,gx,sizeof(gx));const float raster[4][4]{{1,0,0,0},{0,1.3333332539f,0,0},{0,0,1,0},{0,0,0,1}};
            check(composeRasterProjection(rasterCamera,raster)&&rasterCamera.rasterProjectionValid,"measured raster composition rejected");
            float executedProjection[4][4]{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)executedProjection[c][r]=gx[r][c]+(r==2?gx[3][c]:0.f);
            auto executedCamera=camera();std::memcpy(executedCamera.projection,gx,sizeof(gx));check(composeExecutedRasterProjection(executedCamera,raster,executedProjection),"actual executed projection composition rejected");
            auto reasserted=executedCamera;check(sameQueuedWorldView(executedCamera,reasserted),"exact queued camera was treated as a world transition");
            reasserted.view[0][3]=.01f;check(!sameQueuedWorldView(executedCamera,reasserted),"changed queued view was retained as one pass");reasserted=executedCamera;++reasserted.room;check(!sameQueuedWorldView(executedCamera,reasserted),"room change retained old world phase");reasserted=executedCamera;++reasserted.globalAddress;check(!sameQueuedWorldView(executedCamera,reasserted),"global allocation change retained old world phase");
            check(std::memcmp(executedCamera.rasterProjection,rasterCamera.rasterProjection,sizeof(rasterCamera.rasterProjection))==0,"executed native projection changed raster registration");
            auto adjustedProjection=executedCamera;executedProjection[3][2]-=.25f;check(composeExecutedRasterProjection(adjustedProjection,raster,executedProjection)&&std::abs(adjustedProjection.rasterProjection[2][3]-rasterCamera.rasterProjection[2][3]+.25f)<.00001f,"native executed depth rounding was discarded");executedProjection[3][2]+=.25f;
            static QueuedWorldCameras queued;auto queueCamera=rasterCamera;queueCamera.view[0][3]=11;queued.store(0x10000,45,gx,queueCamera);
            auto laterCamera=queueCamera;laterCamera.view[0][3]=-120;NativeSnapshot recovered{};uint64_t queuedAt{};
            check(queued.take(0x10000,gx,recovered,queuedAt)&&queuedAt==45&&recovered.view[0][3]==11&&recovered.view[0][3]!=laterCamera.view[0][3],"queued view drifted to later pG camera");
            check(!queued.take(0x10000,gx,recovered,queuedAt),"queued camera was reused by a second consumer");queued.store(0x10000,70,gx,laterCamera);
            check(queued.take(0x10000,gx,recovered,queuedAt)&&recovered.view[0][3]==-120&&queuedAt==70,"recycled queue address retrieved old camera");
            float changedProjection[4][4]{};std::memcpy(changedProjection,gx,sizeof(gx));changedProjection[0][0]+=.01f;queued.store(0x10000,80,gx,queueCamera);
            check(!queued.take(0x10000,changedProjection,recovered,queuedAt),"recycled record with different bytes accepted old camera");
            for(uintptr_t i=0;i<20000;++i)queued.store(0x20000+i*4,90+i,gx,queueCamera);check(!queued.take(0x20000,gx,recovered,queuedAt)&&queued.take(0x20000+19999*4,gx,recovered,queuedAt),"queue camera mailbox was unbounded or discarded newest entry");
            queued.store(0xA0000,90,gx,queueCamera,3);check(!queued.take(0xA0000,gx,recovered,queuedAt,4),"disabled capture generation leaked old camera on resume");queued.store(0xA0000,91,gx,laterCamera,4);check(queued.take(0xA0000,gx,recovered,queuedAt,4)&&recovered.view[0][3]==-120,"fresh resumed camera was not recovered");
            static QueuedWorldCameras denseQueue;
            for(uintptr_t i=0;i<512;++i){auto distinct=queueCamera;distinct.view[0][3]=float(i);denseQueue.store(0x600000+i*76,100+i,gx,distinct);}
            for(uintptr_t i=0;i<512;++i)check(denseQueue.take(0x600000+i*76,gx,recovered,queuedAt)&&recovered.view[0][3]==float(i),"dense queued frame lost or mismatched a camera");
            LARGE_INTEGER queueFrequency{},queueStart{},queueEnd{};QueryPerformanceFrequency(&queueFrequency);QueryPerformanceCounter(&queueStart);uint64_t queueChecks{};
            for(uintptr_t i=0;i<100000;++i){uintptr_t record=0x600000+(i%512)*76;denseQueue.store(record,i,gx,queueCamera);check(denseQueue.take(record,gx,recovered,queuedAt)&&queuedAt==i,"queue lookup failed during stress");queueChecks+=queuedAt;}
            QueryPerformanceCounter(&queueEnd);check(queueChecks>0,"queue benchmark was optimized away");
            std::printf("PASS:512-record dense queue retains exact cameras;100000 bounded store/take pairs average%.3fus (excludes native memory sampling).\n",double(queueEnd.QuadPart-queueStart.QuadPart)*1000000/queueFrequency.QuadPart/100000);
            std::puts("PASS: queued native camera survives later pG movement, record reuse/byte mismatch are safe, exact executed depth is preserved.");
            {
                std::array<uint8_t,0x4c> record{};uint32_t consumer=0x12345678,kind=0;
                std::memcpy(record.data(),&consumer,4);std::memcpy(record.data()+8,gx,sizeof(gx));std::memcpy(record.data()+0x48,&kind,4);
                const uintptr_t address=reinterpret_cast<uintptr_t>(record.data());
                check(readQueuedProjection(address,consumer,kind,gx),"batched queue record rejected exact bytes");
                check(!readQueuedProjection(address,consumer+1,kind,gx)&&!readQueuedProjection(address,consumer,kind+1,gx)&&!readQueuedProjection(address,consumer,kind,changedProjection),"batched queue record accepted changed callback/kind/matrix");
                check(!readQueuedProjection(0,consumer,kind,gx)&&!readQueuedProjection(UINTPTR_MAX-4,consumer,kind,gx),"batched queue record accepted invalid/overflow address");
                auto guarded=static_cast<uint8_t*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));check(guarded!=nullptr,"allocate queue guard fixture");
                DWORD oldProtect{};check(VirtualProtect(guarded+4096,4096,PAGE_NOACCESS,&oldProtect)!=0,"protect queue guard fixture");
                check(!readQueuedProjection(reinterpret_cast<uintptr_t>(guarded+4096-32),consumer,kind,gx),"batched queue record read across inaccessible page");
                VirtualFree(guarded,0,MEM_RELEASE);
                constexpr unsigned Samples=100000;double times[2]{};
                for(unsigned mode=0;mode<2;++mode){
                    QueryPerformanceCounter(&queueStart);
                    for(unsigned n=0;n<Samples;++n){bool exact{};
                        if(mode)exact=readQueuedProjection(address,consumer,kind,gx);
                        else{uint32_t callback=hookdetail::operand(address),actualKind=hookdetail::operand(address+0x48);float matrix[4][4]{};exact=callback==consumer&&actualKind==kind&&hookdetail::copy(address+8,matrix,sizeof(matrix))&&std::memcmp(matrix,gx,sizeof(matrix))==0;}
                        check(exact,"record copy benchmark lost exact queue record");
                    }
                    QueryPerformanceCounter(&queueEnd);times[mode]=double(queueEnd.QuadPart-queueStart.QuadPart)*1000000/queueFrequency.QuadPart/Samples;
                }
                std::printf("PASS: guarded queue record exact callback/kind/matrix + invalid/overflow/cross-page protection; read-only100000-record benchmark split=%.3fus grouped=%.3fus (record reads only).\n",times[0],times[1]);
            }
            check(std::abs(rasterCamera.rasterProjection[0][0]-1.0711109638f)<1e-6f&&std::abs(rasterCamera.rasterProjection[1][1]-1.9041973352f)<1e-6f&&std::abs(rasterCamera.rasterProjection[2][2]+1.0001000166f)<1e-6f,"measured raster or GX depth conversion changed");
            renderer.setAvatarOrigin({},0);renderer.setSceneOrigin({},0);renderer.setDynamicOrigin({},0);check(renderer.setMesh(1,{triangle}),"raster avatar fixture");renderer.setMesh(2,{});renderer.setMesh(3,{});
            RenderPlacement rasterPlacement{};rasterPlacement.origin={0,0,-4000};rasterPlacement.worldUnitsPerBlock=1000;
            draw(renderer,device,rasterCamera,1,true,&rasterPlacement);auto registered=readback(device);
            auto hostProjection=[&](){D3DMATRIX identity{},projection{};for(int i=0;i<4;++i)identity.m[i][i]=1;projection.m[0][0]=1.0711109638f;projection.m[1][1]=1.9041973352f;projection.m[2][2]=-1.0001000166f;projection.m[2][3]=-1;projection.m[3][2]=-100.0100021362f;
                hr(device->SetVertexShader(nullptr),"native fixture VS");hr(device->SetPixelShader(nullptr),"native fixture PS");hr(device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE),"native fixture FVF");hr(device->SetTransform(D3DTS_WORLD,&identity),"native fixture world");hr(device->SetTransform(D3DTS_VIEW,&identity),"native fixture view");hr(device->SetTransform(D3DTS_PROJECTION,&projection),"native fixture projection");
                device->SetTexture(0,nullptr);device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);};
            struct HostVertex{float x,y,z;uint32_t color;};const HostVertex hostBody[]{{-1000,-1000,-4000,0xffff0000},{1000,-1000,-4000,0xffff0000},{0,1000,-4000,0xffff0000}};
            hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"native reference clear");hr(device->BeginScene(),"native reference begin");hostProjection();hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,hostBody,sizeof(HostVertex)),"native body reference");hr(device->EndScene(),"native reference end");check(readback(device).pixels==registered.pixels,"bridge geometry did not register with measured native raster");
            const HostVertex hostWall[]{{-1200,-1200,-3500,0xff0000ff},{0,-1200,-3500,0xff0000ff},{0,1200,-3500,0xff0000ff},{-1200,-1200,-3500,0xff0000ff},{0,1200,-3500,0xff0000ff},{-1200,1200,-3500,0xff0000ff}};
            hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"registered wall clear");hr(device->BeginScene(),"registered wall begin");hostProjection();hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,hostWall,sizeof(HostVertex)),"native wall reference");bool validWorld=false;check(renderer.render(device,rasterCamera,rasterPlacement,&validWorld)&&validWorld,"registered body world proof");hr(device->EndScene(),"registered wall end");auto occluded=readback(device);check(occluded.red>100&&occluded.red<registered.red*.65,"measured native wall did not occlude registered body");writeBmp(occluded,"build\\tests\\renderer-native-raster.bmp");
            RenderPlacement proofPlacement{};proofPlacement.origin={0,0,-4};proofPlacement.worldUnitsPerBlock=1;auto clippedProof=camera();clippedProof.view[1][3]=-100;
            hr(device->BeginScene(),"culled proof begin");validWorld=false;check(!renderer.render(device,clippedProof,proofPlacement,&validWorld)&&validWorld,"culled avatar invalidated world depth proof");hr(device->EndScene(),"culled proof end");
            proofPlacement.drawAvatar=proofPlacement.drawScene=proofPlacement.drawDynamic=false;validWorld=false;hr(device->BeginScene(),"empty proof begin");check(!renderer.render(device,camera(),proofPlacement,&validWorld)&&validWorld,"empty first-person world proof failed");hr(device->EndScene(),"empty proof end");
            IDirect3DSurface9* realDepth{};hr(device->GetDepthStencilSurface(&realDepth),"proof depth");hr(device->SetDepthStencilSurface(nullptr),"remove proof depth");validWorld=true;hr(device->BeginScene(),"missing depth proof begin");check(!renderer.render(device,camera(),proofPlacement,&validWorld)&&!validWorld,"missing depth accepted as empty first-person world");hr(device->EndScene(),"missing depth proof end");hr(device->SetDepthStencilSurface(realDepth),"restore proof depth");realDepth->Release();
            RenderBatch sceneOnly=triangle;sceneOnly.textureId=0;for(auto& vertex:sceneOnly.vertices)vertex.color=0xff00ff00;renderer.setMesh(2,{sceneOnly});renderer.setAvatarOrigin({0,10000,-4},77);renderer.setSceneOrigin({0,0,-4},77);proofPlacement.drawAvatar=proofPlacement.drawScene=true;proofPlacement.hostEpoch=77;
            draw(renderer,device,camera(),1,true,&proofPlacement);auto remainingScene=readback(device);unsigned greenPixels{};for(auto pixel:remainingScene.pixels)if((pixel&0xffffff)==0x00ff00)++greenPixels;check(greenPixels>500,"clipped avatar suppressed visible world blocks");renderer.setAvatarOrigin({},0);renderer.setSceneOrigin({},0);renderer.setMesh(2,{});
            auto malformedRaster=rasterCamera;malformedRaster.rasterProjection[1][1]=std::numeric_limits<float>::quiet_NaN();validWorld=true;check(!renderer.render(device,malformedRaster,rasterPlacement,&validWorld)&&!validWorld,"nonfinite measured raster accepted");
            std::puts("PASS: measured native raster registration/depth, culled and empty valid-world proof, missing-depth rejection, and visible scene while avatar is clipped.");
            renderer.onDeviceLost();draw(renderer,device,camera(),1);check(readback(device).red>500,"GPU resource recreation failed");
            auto inactive=camera();inactive.cutscene=true;draw(renderer,device,inactive,1,false);check(readback(device).visible==0,"cutscene gate rendered avatar");
            auto clipped=camera();clipped.view[1][3]=-100;draw(renderer,device,clipped,1,false);check(readback(device).visible==0,"entirely off-screen avatar was drawn");
            RenderPlacement behind{};behind.origin={0,0,4};behind.worldUnitsPerBlock=1;draw(renderer,device,camera(),1,false,&behind);check(readback(device).visible==0,"behind-camera avatar was drawn");
            RenderBatch crossing{7,0,{{-10,-10,0,0,0,1,0,1,0xffffffff},{10,-10,0,0,0,1,1,1,0xffffffff},{0,10,0,0,0,1,0.5f,0,0xffffffff}}};
            check(renderer.setMesh(1,{crossing}),"crossing triangle rejected");draw(renderer,device,camera(),1);check(readback(device).red>1000,"viewport-crossing triangle with outside vertices was culled");
            RenderBatch body{7,0,{{-0.3f,0,0,0,0,1,0,1,0xffffffff},{0.3f,0,0,0,0,1,1,1,0xffffffff},{0,1.8f,0,0,0,1,0.5f,0,0xffffffff}}};
            check(renderer.setMesh(1,{body}),"body regression fixture rejected");
            auto measured=measuredCamera();RenderPlacement bodyPlacement{};bodyPlacement.origin=measured.position;bodyPlacement.worldUnitsPerBlock=100;
            draw(renderer,device,measured,1,false,&bodyPlacement);check(readback(device).visible==0,"old 100-unit avatar failed the real-camera clipping regression");
            bodyPlacement.worldUnitsPerBlock=1000;draw(renderer,device,measured,1,true,&bodyPlacement);image=readback(device);check(image.red>100,"1000-unit avatar produced no pixels under the measured real camera");writeBmp(image,"build\\tests\\renderer-re4-scale.bmp");
            std::printf("PASS: measured RE4 camera rejects scale100 and renders scale1000 (%u red pixels); crossing triangles survive clipping.\n",image.red);
            if(argc>=2) {
                uint32_t captureCategory=argc>=4?static_cast<uint32_t>(std::stoul(argv[3])):1;
                renderer.clear();CaptureBounds captureBounds;std::vector<RenderBatch> actualModel;check(loadCapture(renderer,argv[1],captureCategory,&captureBounds,&actualModel),"capture did not contain requested category");
                RenderPlacement capturePlacement{};capturePlacement.origin={0,0,-4};capturePlacement.worldUnitsPerBlock=1;capturePlacement.drawAvatar=captureCategory==1;capturePlacement.drawScene=captureCategory==2;capturePlacement.drawDynamic=captureCategory==3;
                if(captureCategory!=1){check(captureBounds.valid,"dynamic/scene capture contains no vertices");capturePlacement.origin.x-=(captureBounds.min.x+captureBounds.max.x)*.5f;capturePlacement.origin.y-=(captureBounds.min.y+captureBounds.max.y)*.5f;capturePlacement.origin.z-=(captureBounds.min.z+captureBounds.max.z)*.5f;}
                if(captureBounds.crumbling){
                    // The actual extracted model supplies both its retained
                    // opaque depth and the original destruction overlay UVs.
                    // A neutral surface isolates the real destroy-stage texels.
                    Renderer base;check(base.setMesh(captureCategory,std::move(actualModel)),"actual destruction base rejected");
                    draw(base,device,camera(),1,true,&capturePlacement);auto before=readback(device);
                    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"actual crumbling clear");hr(device->BeginScene(),"actual crumbling begin");
                    check(base.render(device,camera(),capturePlacement),"actual destruction base draw");check(renderer.render(device,camera(),capturePlacement),"actual destruction overlay draw");hr(device->EndScene(),"actual crumbling end");image=readback(device);
                    unsigned changed{};for(size_t p=0;p<image.pixels.size();++p)if((image.pixels[p]&0xffffff)!=(before.pixels[p]&0xffffff))++changed;
                    check(changed>100,"actual destroy-stage texels did not affect retained model surface");std::printf("PASS: actual Minecraft destruction overlay changes %u retained model pixels, original negative/repeating UVs preserved.\n",changed);
                }else{draw(renderer,device,camera(captureCategory==1),1,true,&capturePlacement);image=readback(device);}
                check(image.visible>100,"captured Minecraft geometry produced no visible pixels");
                writeBmp(image,argc>=3?argv[2]:"build\\tests\\renderer-avatar.bmp");std::printf("PASS: captured Minecraft category %u rendered %u non-black pixels.\n",captureCategory,image.visible);
                if(captureBounds.translucent){auto prematureCapture=repeatedProjectionOrdering(renderer,device,false,capturePlacement,camera(captureCategory==1)),completeCapture=repeatedProjectionOrdering(renderer,device,true,capturePlacement,camera(captureCategory==1));check(completeCapture.visible>prematureCapture.visible+100,"real translucent capture did not survive complete-world ordering");
                    std::string name=argc>=3?argv[2]:"build\\tests\\renderer-avatar.bmp";writeBmp(prematureCapture,(name+"-premature.bmp").c_str());writeBmp(completeCapture,(name+"-complete.bmp").c_str());std::printf("PASS: actual category%u capture late-native ordering: premature=%u complete=%u non-black pixels.\n",captureCategory,prematureCapture.visible,completeCapture.visible);}
                if(captureCategory==2&&captureBounds.translucent){
                    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"captured water depth clear");D3DRECT occludingHalf{0,0,Width/2,Height};hr(device->Clear(1,&occludingHalf,D3DCLEAR_ZBUFFER,0,0,0),"captured water native wall depth");hr(device->BeginScene(),"captured water wall begin");check(renderer.render(device,camera(),capturePlacement),"captured water wall submission");hr(device->EndScene(),"captured water wall end");auto retainedWater=readback(device);unsigned hidden{},visible{};for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)if(retainedWater.pixels[y*Width+x]&0xffffff){if(x<Width/2)++hidden;else++visible;}
                    check(hidden<100&&visible>100,"actual water bypassed retained native wall depth");std::string name=argc>=3?argv[2]:"build\\tests\\renderer-water.bmp";writeBmp(retainedWater,(name+"-occluded.bmp").c_str());std::printf("PASS: actual water obeys retained host wall depth: hiddenHalf=%u visibleHalf=%u.\n",hidden,visible);
                }
                if(captureCategory==1){bodyPlacement.worldUnitsPerBlock=100;draw(renderer,device,measured,1,false,&bodyPlacement);check(readback(device).visible==0,"captured avatar at old scale escaped the measured-camera regression");
                bodyPlacement.worldUnitsPerBlock=1000;draw(renderer,device,measured,1,true,&bodyPlacement);image=readback(device);check(image.visible>100,"captured avatar at corrected scale produced no measured-camera pixels");writeBmp(image,"build\\tests\\renderer-re4-avatar.bmp");std::printf("PASS: captured avatar with measured RE4 camera rendered %u non-black pixels at scale1000.\n",image.visible);
                }
            }
            renderer.clear();check(!renderer.hasAvatar(),"clear retained stale avatar");draw(renderer,device,camera(),1,false);
            HudRenderer hud;HudData hudData{};hudData.width=2;hudData.height=2;hudData.flags=1;
            // Raw framebuffer rows start at the bottom, and RGB is already
            // premultiplied. The half red bottom-left pixel must remain 128
            // over blue, rather than being multiplied by alpha a second time.
            hudData.rgba={128,0,0,128,0,255,0,255,0,0,255,255,255,255,255,255};hud.update(hudData);
            hr(device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE),"HUD sentinel zwrite");hr(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"HUD sentinel blend");hr(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA),"HUD sentinel source blend");
            hr(device->SetRenderState(D3DRS_SRGBWRITEENABLE,TRUE),"HUD sentinel sRGB write");hr(device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE),"HUD sentinel sRGB sample");
            hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff0000ff,0,0),"HUD blue background");hr(device->BeginScene(),"HUD BeginScene");check(hud.render(device),"HUD render rejected");hr(device->EndScene(),"HUD EndScene");image=readback(device);
            check((image.pixels[(Height/4)*Width+Width/4]&0xffffff)==0x0000ff,"HUD bottom-up top-left pixel wrong");
            check((image.pixels[(Height/4)*Width+3*Width/4]&0xffffff)==0xffffff,"HUD RGBA white pixel wrong");
            blended=image.pixels[(3*Height/4)*Width+Width/4];check(std::abs(int((blended>>16)&255)-128)<=1&&std::abs(int(blended&255)-127)<=1&&((blended>>8)&255)==0,"HUD premultiplied alpha or row flip wrong");
            check((image.pixels[(3*Height/4)*Width+3*Width/4]&0xffffff)==0x00ff00,"HUD RGBA green pixel wrong");
            for(auto state:{D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_SRGBWRITEENABLE}){hr(device->GetRenderState(state,&value),"HUD restored state");DWORD expected=state==D3DRS_SRCBLEND?static_cast<DWORD>(D3DBLEND_SRCALPHA):state==D3DRS_ALPHABLENDENABLE?FALSE:TRUE;check(value==expected,"HUD leaked render state");}
            hr(device->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&value),"HUD restored sRGB sampler");check(value==TRUE,"HUD leaked sRGB sampler");
            hr(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE),"restore harness sRGB write");hr(device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE),"restore harness sRGB sample");
            hud.onDeviceLost();hr(device->BeginScene(),"HUD recreation BeginScene");check(hud.render(device),"HUD device recreation failed");hr(device->EndScene(),"HUD recreation EndScene");hud.clear();check(!hud.render(device),"HUD clear retained stale framebuffer");
            hudData.rgba.pop_back();hud.update(hudData);check(!hud.render(device),"HUD malformed RGBA length accepted");
            std::puts("PASS: HUD bottom-up RGBA, premultiplied alpha, render state restoration, GPU recreation and stale clearing.");
            HudData fullHud{};fullHud.width=1920;fullHud.height=1080;fullHud.flags=1;fullHud.rgba.resize(size_t(fullHud.width)*fullHud.height*4);
            for(size_t n=0;n<fullHud.rgba.size();n+=4){fullHud.rgba[n]=255;fullHud.rgba[n+3]=255;}
            LARGE_INTEGER frequency{},started{},finished{};QueryPerformanceFrequency(&frequency);std::vector<double> uploadTimes;
            for(unsigned n=0;n<100;++n){auto update=fullHud;QueryPerformanceCounter(&started);hud.update(std::move(update));hr(device->BeginScene(),"1080p HUD begin");check(hud.render(device),"1080p HUD upload/render failed");hr(device->EndScene(),"1080p HUD end");QueryPerformanceCounter(&finished);uploadTimes.push_back(double(finished.QuadPart-started.QuadPart)*1000/frequency.QuadPart);}
            image=readback(device);check(image.red==Width*Height,"repeated1080p HUD uploads changed source colour or left stale pixels");std::sort(uploadTimes.begin(),uploadTimes.end());
            std::printf("PASS:100 native1920x1080 HUD uploads retain every output pixel; CPU median=%.3fms p95=%.3fms max=%.3fms (includes upload/draw, excludes source decoding).\n",uploadTimes[50],uploadTimes[95],uploadTimes.back());
        }
        device->Release();device=nullptr;api->Release();api=nullptr;DestroyWindow(window);window=nullptr;
        std::printf("PASS: production D3D9 renderer textured pixels, host depth occlusion, state restoration, malformed geometry rejection, device-resource recreation and cutscene gating.\n");return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"Renderer verification failed: %s\n",error.what());
        if(device)device->Release();if(api)api->Release();if(window)DestroyWindow(window);return 1;
    }
}
