#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <psapi.h>
#include "../native/renderer.hpp"
#include "../native/vehicle_display_color.hpp"
#include "../native/vehicle_deformation.hpp"
#include "../native/vehicle_texture_storage.hpp"
#include "../native/transport.hpp"
#include "../native/hud_renderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <atomic>
#include <cstdlib>
#include <new>

// Deny a full-size C++ allocation only while transferring an already completed
// packet. A copying implementation must fail; an ownership transfer succeeds.
namespace {std::atomic<size_t> deniedAllocationSize{};}
void* operator new(size_t bytes){if(deniedAllocationSize.load()&&bytes>=deniedAllocationSize.load())throw std::bad_alloc();if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}

using namespace re4craft;
namespace {
constexpr UINT Width=320,Height=240,TextureSide=4096;
void check(bool okay,const char* operation){if(!okay)throw std::runtime_error(operation);}
void hr(HRESULT status,const char* operation){if(FAILED(status)){char message[256]{};std::snprintf(message,sizeof(message),"%s: HRESULT0x%08lX",operation,static_cast<unsigned long>(status));throw std::runtime_error(message);}}
struct Image {std::vector<uint32_t> pixels;size_t visible{};};
size_t privateBytes(){
    static auto query=reinterpret_cast<decltype(&GetProcessMemoryInfo)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"K32GetProcessMemoryInfo"));
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    return query&&query(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters))?counters.PrivateUsage:0;
}
Image readback(IDirect3DDevice9* device){
    IDirect3DSurface9 *target{},*copy{};
    hr(device->GetRenderTarget(0,&target),"readback target");D3DSURFACE_DESC desc{};
    HRESULT status=target->GetDesc(&desc);
    if(FAILED(status)){target->Release();hr(status,"readback description");}
    status=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr);
    if(FAILED(status)){target->Release();hr(status,"readback allocation");}
    status=device->GetRenderTargetData(target,copy);target->Release();
    if(FAILED(status)){copy->Release();hr(status,"readback transfer");}
    D3DLOCKED_RECT locked{};status=copy->LockRect(&locked,nullptr,D3DLOCK_READONLY);
    if(FAILED(status)){copy->Release();hr(status,"readback lock");}
    Image image;image.pixels.resize(size_t(desc.Width)*desc.Height);
    for(UINT y=0;y<desc.Height;++y){const auto* row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(locked.pBits)+y*locked.Pitch);
        for(UINT x=0;x<desc.Width;++x){uint32_t pixel=row[x];image.pixels[size_t(y)*desc.Width+x]=pixel;if(pixel&0xffffff)++image.visible;}}
    copy->UnlockRect();copy->Release();return image;
}
void bmp(const Image& image,const char* path,UINT width=Width,UINT height=Height){
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+width*height*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-LONG(height);info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=width*height*4;
    std::ofstream out(path,std::ios::binary);check(bool(out),"open BMP");out.write(reinterpret_cast<const char*>(&file),sizeof(file));out.write(reinterpret_cast<const char*>(&info),sizeof(info));out.write(reinterpret_cast<const char*>(image.pixels.data()),static_cast<std::streamsize>(image.pixels.size()*4));check(bool(out),"write BMP");
}
NativeSnapshot camera(){
    NativeSnapshot native{};native.valid=native.alive=native.playing=true;native.view[0][0]=native.view[1][1]=native.view[2][2]=1;
    native.projection[0][0]=2.41421356f/(float(Width)/Height);native.projection[1][1]=2.41421356f;
    native.projection[2][2]=-1.f/99;native.projection[2][3]=-100.f/99;native.projection[3][2]=-1;return native;
}
RenderPlacement placement(){RenderPlacement p{};p.origin={0,0,-4};p.worldUnitsPerBlock=1;p.drawScene=p.drawDynamic=false;return p;}
// Explicitly synthetic GPU test geometry. These triangles are not a Covet model.
RenderBatch tile(UINT x,UINT y){
    const float u=(float(x)+.5f)/TextureSide,v=(float(y)+.5f)/TextureSide;
    return {7,BatchDoubleSided,{{-1,-1,0,0,0,1,u,v,0xffffffff},{1,-1,0,0,0,1,u,v,0xffffffff},{0,1,0,0,0,1,u,v,0xffffffff}}};
}
uint32_t sourceColor(UINT x,UINT y){
    const uint32_t r=(x*17+y*3+31)&255,g=((x*5)^(y*11)^71)&255,b=(x+y*13+137)&255;
    const uint32_t alpha=192+((x*3+y*7)&63);
    return (alpha<<24)|(r<<16)|(g<<8)|b;
}
Image draw(Renderer& renderer,IDirect3DDevice9* device,float depth=1,const NativeSnapshot& native=camera(),const RenderPlacement& p=placement(),uint32_t clearColor=0xff000000,double presentationMs=-1){
    hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,clearColor,depth,0),"clear");hr(device->BeginScene(),"begin scene");
    bool worldValid{};bool rendered=renderer.render(device,native,p,&worldValid,presentationMs);hr(device->EndScene(),"end scene");if(!rendered||!worldValid)std::fprintf(stderr,"%s\n",renderer.describeFrame(device,native,p).c_str());check(rendered&&worldValid,"production renderer rejected valid synthetic mesh/pass");return readback(device);
}
void nativeWall(IDirect3DDevice9* device,float depth=.1f,float width=Width/2.f){
    struct ScreenVertex {float x,y,z,rhw;uint32_t color;};
    const ScreenVertex vertices[6]{{0,0,depth,1,0xff123456},{width,0,depth,1,0xff123456},{width,float(Height),depth,1,0xff123456},{0,0,depth,1,0xff123456},{width,float(Height),depth,1,0xff123456},{0,float(Height),depth,1,0xff123456}};
    hr(device->SetVertexShader(nullptr),"wall vertex shader");hr(device->SetPixelShader(nullptr),"wall pixel shader");hr(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"wall FVF");
    device->SetTexture(0,nullptr);device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);device->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    device->SetRenderState(D3DRS_LIGHTING,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_ZENABLE,TRUE);device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    hr(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(ScreenVertex)),"retained native wall draw");
}
void textureChecks(Renderer& renderer,IDirect3DDevice9* device){
    const std::array<std::array<UINT,2>,7> texels{{{0,0},{1,0},{2047,2048},{4095,4095},{4094,1},{3171,2297},{0,4095}}};
    const uint32_t mask=0xffffff;
    for(auto at:texels){check(renderer.setMesh(1,{tile(at[0],at[1])}),"4k synthetic triangle");auto image=draw(renderer,device);uint32_t actual=image.pixels[(Height/2)*Width+Width/2]&mask,expected=sourceColor(at[0],at[1])&mask;
        if(actual!=expected){char message[192]{};std::snprintf(message,sizeof(message),"4k texel(%u,%u) changed: expected%08X observed%08X",at[0],at[1],expected,actual);throw std::runtime_error(message);}
        check(image.visible>500,"4k texture produced too few pixels");}
}
void idleRetirementChecks(Renderer& renderer,IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters){
    const auto before=renderer.resourceUsage();check(before.gpuTextureCount&&before.cpuTextureCapacityBytes,"idle retirement fixture has no uploaded CPU/GPU texture");
    IDirect3DTexture9* nativeTexture{};hr(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&nativeTexture,nullptr),"native-owned resource before idle retirement");
    hr(device->SetTexture(3,nativeTexture),"bind native-owned resource before idle retirement");hr(device->SetRenderState(D3DRS_FOGENABLE,TRUE),"native state before idle retirement");
    renderer.clear();auto cleared=renderer.resourceUsage();check(cleared.cpuTextureBytes==0&&cleared.cpuTextureCapacityBytes==0,"clear retained full pixel-buffer capacity");
    check(cleared.pendingClear&&cleared.gpuTextureCount==before.gpuTextureCount,"clear retired Direct3D resources on the ingestion path");
    // No render call, BeginScene or replacement eligibility is used here.
    renderer.retirePendingResources();auto retired=renderer.resourceUsage();check(!retired.pendingClear&&retired.gpuTextureCount==0&&retired.gpuMaterialCount==0,"idle menu retained previous session GPU resources");
    IDirect3DBaseTexture9* retained{};DWORD fog{};hr(device->GetTexture(3,&retained),"native resource after idle retirement");hr(device->GetRenderState(D3DRS_FOGENABLE,&fog),"native state after idle retirement");
    check(retained==nativeTexture&&fog==TRUE,"renderer retirement changed native device state or ownership");retained->Release();hr(device->SetTexture(3,nullptr),"unbind native retirement fixture");nativeTexture->Release();
    // A source replacement may complete before the next eligible world pass.
    // Retiring the previous session must keep those exact incoming pixels.
    std::vector<uint8_t> old(4,255);check(renderer.uploadTexture(7,1,1,std::move(old)),"replacement prior packet");renderer.clear();
    std::vector<uint8_t> replacement(size_t(TextureSide)*TextureSide*4);
    for(UINT y=0;y<TextureSide;++y)for(UINT x=0;x<TextureSide;++x){const auto color=sourceColor(x,y);const size_t at=(size_t(y)*TextureSide+x)*4;replacement[at]=uint8_t(color>>16);replacement[at+1]=uint8_t(color>>8);replacement[at+2]=uint8_t(color);replacement[at+3]=uint8_t(color>>24);}
    check(renderer.uploadTexture(7,TextureSide,TextureSide,std::move(replacement)),"new session packet before retirement");renderer.retirePendingResources();
    check(renderer.resourceUsage().cpuTextureBytes==size_t(TextureSide)*TextureSide*4,"retirement discarded new session CPU packet");textureChecks(renderer,device);
    renderer.onDeviceLost();hr(device->Reset(&parameters),"actual Reset after idle retirement");textureChecks(renderer,device);
    std::puts("PASS: completed4096x4096 pixels transfer without a full-size allocation; clear releases CPU capacity, idle retirement releases previous GPU resources without a draw or native state change, and newer exact pixels survive retirement and actual Reset.");
}
void alphaChecks(Renderer& renderer,IDirect3DDevice9* device){
    struct Surface {IDirect3DSurface9* pointer{};~Surface(){if(pointer)pointer->Release();}} original,rgba;
    hr(device->GetRenderTarget(0,&original.pointer),"original native target");
    hr(device->CreateRenderTarget(Width,Height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rgba.pointer,nullptr),"RGBA exact-alpha target");
    hr(device->SetRenderTarget(0,rgba.pointer),"bind exact-alpha target");
    struct Restore {IDirect3DDevice9* device;IDirect3DSurface9* target;~Restore(){device->SetRenderTarget(0,target);}} restore{device,original.pointer};
    D3DSURFACE_DESC desc{};hr(rgba.pointer->GetDesc(&desc),"RGBA target format");check(desc.Format==D3DFMT_A8R8G8B8,"exact alpha test target lacks alpha");
    const std::array<std::array<UINT,2>,7> texels{{{0,0},{1,0},{2047,2048},{4095,4095},{4094,1},{3171,2297},{0,4095}}};
    // Observe sourcealpha through the actual translucent RGB blend against a
    // known black target. Destination alpha is not a texture-preservation
    // contract: this native framebuffer's readback retains its clear alpha.
    for(auto at:texels){auto triangle=tile(at[0],at[1]);triangle.flags|=BatchTranslucent;check(renderer.setMesh(1,{triangle}),"4k translucent alpha mesh");
        auto image=draw(renderer,device,1,camera(),placement(),0);uint32_t actual=image.pixels[(Height/2)*Width+Width/2],source=sourceColor(at[0],at[1]);
        const unsigned expectedAlpha=source>>24;
        for(unsigned shift:{16U,8U,0U}){int expected=int(std::lround(double((source>>shift)&255)*expectedAlpha/255.));int observed=int((actual>>shift)&255);
            if(std::abs(expected-observed)>1){char message[192]{};std::snprintf(message,sizeof(message),"4k translucent texel(%u,%u) alpha blend changed: expectedchannel%d observed%d",at[0],at[1],expected,observed);throw std::runtime_error(message);}}
        check(image.visible>500,"4k translucent texture produced too few pixels");}
}
void compressedStorageChecks(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters){
    // This is controlled sampler geometry and a synthetic4k source, not a
    // vehicle model or live game. The same production ingestion/upload paths
    // preserve every RGB and alpha sample without a retained64MiB CPU copy.
    Renderer renderer(true,true);std::vector<uint8_t> raw(size_t(TextureSide)*TextureSide*4),packed;
    for(UINT y=0;y<TextureSide;++y)for(UINT x=0;x<TextureSide;++x){const auto color=sourceColor(x,y);const size_t at=(size_t(y)*TextureSide+x)*4;
        raw[at]=uint8_t(color>>16);raw[at+1]=uint8_t(color>>8);raw[at+2]=uint8_t(color);raw[at+3]=uint8_t(color>>24);}
    check(compressedTexture(raw,packed),"synthetic original4k compression");const auto retained=packed.size();std::vector<uint8_t>().swap(raw);
    check(renderer.uploadVehicleTexture(7,TextureSide,TextureSide,std::move(packed),4,true),"compressed4k vehicle ownership transfer");
    auto usage=renderer.resourceUsage();check(usage.decodedTextureBytes==size_t(TextureSide)*TextureSide*4&&usage.cpuTextureBytes==retained&&usage.cpuTextureCapacityBytes==retained,"compressed original dimensions/resident capacity changed");
    check(renderer.setMesh(1,{tile(3171,2297)}),"compressed allocation-failure sample mesh");
    auto denyDecodedAllocation=[&](){
        hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff071d43,1,0),"compressed denial clear");hr(device->BeginScene(),"compressed denial begin");
        bool valid=true,rendered{};
        struct Fence {Fence(){deniedAllocationSize=size_t(TextureSide)*TextureSide*4;}~Fence(){deniedAllocationSize=0;}};
        {Fence fence;rendered=renderer.render(device,camera(),placement(),&valid);}
        hr(device->EndScene(),"compressed denial end");check(!rendered&&!valid,"denied original decode allocation published a world pass");
        const auto image=readback(device);check(std::all_of(image.pixels.begin(),image.pixels.end(),[](uint32_t p){return (p&0xffffff)==0x071d43;}),"decode allocation failure drew invented or partial pixels");
        check(renderer.resourceUsage().gpuTextureCount==0&&renderer.resourceUsage().cpuTextureBytes==retained,"decode failure discarded source or leaked a GPU map");
    };
    denyDecodedAllocation();textureChecks(renderer,device);alphaChecks(renderer,device);
    check(renderer.resourceUsage().cpuTextureCapacityBytes==retained,"GPU upload retained decoded scratch capacity");
    renderer.onDeviceLost();hr(device->Reset(&parameters),"compressed original actual Reset");denyDecodedAllocation();textureChecks(renderer,device);alphaChecks(renderer,device);
    renderer.clear();renderer.retirePendingResources();usage=renderer.resourceUsage();check(!usage.cpuTextureBytes&&!usage.cpuTextureCapacityBytes&&!usage.decodedTextureBytes&&!usage.gpuTextureCount,"compressed resource retirement retained storage");
    std::vector<uint8_t> corrupt{1,2,3};check(renderer.uploadVehicleTexture(7,TextureSide,TextureSide,std::move(corrupt),4,true),"internal corrupt map diagnostic fixture ingestion");
    check(renderer.setMesh(1,{tile(0,0)}),"internal corrupt sampler fixture");hr(device->BeginScene(),"corrupt map begin");bool valid=true;
    check(!renderer.render(device,camera(),placement(),&valid)&&!valid,"corrupt compressed map published pixels");hr(device->EndScene(),"corrupt map end");
    std::printf("PASS: lossless4k storage %zu/%zu bytes; seven exact RGB/alpha samples, allocation denial fails closed and recovers on first upload/Reset, decoded scratch not retained, corrupt storage rejected at GPU upload. Synthetic sampler geometry only.\n",retained,size_t(TextureSide)*TextureSide*4);
}
void vehicleMaterialChecks(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters){
    Renderer renderer(true),minecraft;
    const uint8_t palette[8]{255,0,0,255,0,0,255,255};const uint8_t fullOpacity[2]{255,255},zeroOpacity[2]{0,0};
    const uint8_t flatNormal[4]{128,128,128,128},turnedNormal[4]{240,128,240,128};
    const uint8_t base[8]{200,120,40,0,200,120,40,0};
    check(!minecraft.uploadTexture(90,2,1,fullOpacity,2,1),"Minecraft accepted compact vehicle mask");
    check(!minecraft.uploadTexture(90,2,1,base,8,260),"Minecraft accepted vehicle sRGB extension");
    check(renderer.uploadTexture(90,2,1,palette,8,260),"actual sRGB palette upload");
    check(renderer.uploadTexture(91,2,1,fullOpacity,2,1),"exact R8 opacity upload");
    check(renderer.uploadTexture(92,2,1,flatNormal,4,2),"exact RG8 normal upload");
    check(renderer.uploadTexture(93,2,1,base,8,260),"sRGB base alpha0 upload");
    check(!renderer.uploadTexture(94,2,1,base,7,260),"truncated compact format accepted");
    VehicleMaterial header{};header.epoch=1;header.id=1;header.layerCount=1;header.flags=1;
    for(unsigned i=0;i<3;i++){header.paintColor[i][i]=.8f;header.paintColor[i][3]=1;header.paintData[i][0]=.4f;}
    VehicleMaterialLayer layer{};layer.maps[1]=90;layer.flags=4;layer.mapUV1Mask=2;
    for(float& c:layer.baseColor)c=1;layer.opacity=1;layer.roughness=.6f;layer.ao=1;layer.normalStrength=1;layer.detailNormalStrength=1;layer.detailScaleX=layer.detailScaleY=1;
    auto checkLayer=layer;checkLayer.metallic=1;check(renderer.setVehicleMaterial(header,{checkLayer}),"valid exact material factor endpoint");
    checkLayer.metallic=1.001f;check(!renderer.setVehicleMaterial(header,{checkLayer}),"out-of-contract metallic factor accepted");
    checkLayer=layer;checkLayer.normalStrength=16;check(renderer.setVehicleMaterial(header,{checkLayer}),"valid exact normal strength endpoint");
    checkLayer.normalStrength=16.001f;check(!renderer.setVehicleMaterial(header,{checkLayer}),"out-of-contract normal strength accepted");
    checkLayer=layer;checkLayer.detailScaleX=-1024;check(renderer.setVehicleMaterial(header,{checkLayer}),"valid reflected detail scale endpoint");
    checkLayer.detailScaleX=-1024.01f;check(!renderer.setVehicleMaterial(header,{checkLayer}),"out-of-contract detail scale accepted");
    checkLayer=layer;checkLayer.emissiveNits=100000;check(renderer.setVehicleMaterial(header,{checkLayer}),"valid emissive bound endpoint");
    checkLayer.emissiveNits=100001;check(!renderer.setVehicleMaterial(header,{checkLayer}),"out-of-contract emissive intensity accepted");
    checkLayer=layer;checkLayer.maps[0]=65535;check(renderer.setVehicleMaterial(header,{checkLayer}),"valid map ID bound endpoint");
    checkLayer.maps[0]=65536;check(!renderer.setVehicleMaterial(header,{checkLayer}),"out-of-contract map ID accepted");
    auto batch=tile(0,0);batch.textureId=0;batch.materialId=1;
    for(auto& v:batch.vertices){v.u=.25f;v.v=.5f;v.color=0xffffffff;}
    batch.secondaryUV.assign(3,{.75f,.5f});
    auto broadUV=batch;broadUV.secondaryUV.assign(3,{32.f,-32.f});check(renderer.setVehicleFrame({broadUV},{},1,1),"legitimate UV2 outside16 was rejected");
    broadUV.secondaryUV[0][0]=1e6f+1;check(!renderer.setVehicleFrame({broadUV},{},1,1),"out-of-contract UV2 above1e6 accepted");
    broadUV.secondaryUV[0][0]=std::numeric_limits<float>::infinity();check(!renderer.setVehicleFrame({broadUV},{},1,1),"nonfinite UV2 accepted");
    auto publish=[&](){check(renderer.setVehicleMaterial(header,{layer}),"material descriptor");check(renderer.setVehicleFrame({batch},{},1,1),"UV2 surface frame");renderer.setVehiclePose({0,0,-4},{},1);};
    auto center=[&](const Image& image){return image.pixels[(Height/2)*Width+Width/2]&0xffffff;};
    publish();auto blueImage=draw(renderer,device);uint32_t blue=center(blueImage);std::printf("Paint UV1 fixture RGB=%06X\n",blue);bmp(blueImage,"build\\beamng-render-tests\\paint-uv1-blue.bmp");check((blue&255)>((blue>>16)&255)+50,"actual paint mask did not use independent UV1");
    batch.secondaryUV.assign(3,{.25f,.5f});publish();uint32_t red=center(draw(renderer,device));check(((red>>16)&255)>(red&255)+50,"paint masks do not select actual paint channels");
    layer.flags=4|8;publish();auto paletteRough=draw(renderer,device);layer.flags=4;layer.roughness=.6f*.4f;publish();auto equivalentRough=draw(renderer,device);
    for(unsigned shift:{0U,8U,16U})check(std::abs(int(center(paletteRough)>>shift&255)-int(center(equivalentRough)>>shift&255))<=1,"paint roughness replaced rather than multiplied the actual stage factor");
    layer.roughness=.6f;
    header.flags=0;publish();check(draw(renderer,device).visible>100,"original one-sided front surface was culled");
    auto mirroredPlacement=placement();mirroredPlacement.ySign=-1;check(draw(renderer,device,1,camera(),mirroredPlacement).visible>100,"mirrored native Y mapping culled the original front surface");header.flags=1;
    layer.maps[1]=0;layer.maps[0]=93;layer.flags=0;publish();uint32_t alpha0=center(draw(renderer,device));check(alpha0!=0,"base-map alpha incorrectly became material opacity");
    const uint8_t opaqueBase[8]{200,120,40,255,200,120,40,255};check(renderer.uploadTexture(93,2,1,opaqueBase,8,260),"base alpha update");check(center(draw(renderer,device))==alpha0,"base alpha changed actual PBR opacity");
    header.flags=1|2;layer.opacity=.25f;batch.flags=BatchDoubleSided|BatchTranslucent;publish();uint32_t noBlend=center(draw(renderer,device));
    layer.opacity=1;publish();check(center(draw(renderer,device))==noBlend,"actual BlendOp None was incorrectly alpha blended");
    header.flags=1|2|(1u<<8);layer.maps[2]=91;publish();check(draw(renderer,device).visible>100,"full R8 opacity mask hid surface");
    check(renderer.uploadTexture(91,2,1,zeroOpacity,2,1),"zero R8 opacity update");check(draw(renderer,device).visible==0,"R8 opacity mask not applied to transparent material");
    check(renderer.uploadTexture(91,2,1,fullOpacity,2,1),"restore actual mask for depth test");layer.opacity=.5f;
    auto depthProbe=[&](){hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"lettering depth clear");hr(device->BeginScene(),"lettering depth begin");bool valid{};check(renderer.render(device,camera(),placement(),&valid)&&valid,"lettering source draw");nativeWall(device,.95f,float(Width));hr(device->EndScene(),"lettering depth end");return center(readback(device));};
    publish();check(depthProbe()==0x123456,"translucent material unexpectedly wrote native depth");
    header.flags|=8;publish();check(depthProbe()!=0x123456,"actual translucentZWrite lettering failed to occlude a later farther native surface");
    header.flags=1|2;batch.flags=BatchDoubleSided;publish();check(depthProbe()==0x123456,"BlendOp None incorrectly implied translucent Z write");
    // The one-sided +Z source normal faces away from the directional light;
    // this isolates the cubemap without suppressing legitimate direct lighting.
    header.flags=0;layer.opacity=1;layer.maps[2]=0;layer.maps[5]=0;layer.metallic=1;layer.clearcoat=0;publish();check(draw(renderer,device).visible>100,"metallic source lost environment reflection");
    header.flags|=16;publish();check(draw(renderer,device).visible==0,"actual BlackSkyCubemap kept exterior environment reflections");
    layer.metallic=0;publish();check(draw(renderer,device).visible>100,"black specular cubemap incorrectly removed diffuse ambient illumination");
    header.flags=1;
    check(renderer.uploadTexture(91,2,1,fullOpacity,2,1),"restore actual mask");header.flags=1;batch.flags=BatchDoubleSided;layer.maps[5]=92;
    batch.vertices[0].u=0;batch.vertices[0].v=1;batch.vertices[1].u=1;batch.vertices[1].v=1;batch.vertices[2].u=.5f;batch.vertices[2].v=0;
    publish();uint32_t flat=center(draw(renderer,device));
    check(renderer.uploadTexture(92,2,1,turnedNormal,4,2),"RG8 channel update");uint32_t turned=center(draw(renderer,device));check(flat!=turned,"actual RG8 normal channels did not affect lighting/reflection");
    {
        // Isolate an HDR material contribution to verify the actual shader's
        // display transform. Hard-clipping this highlight would turn it white.
        const auto savedHeader=header;const auto savedLayer=layer;const auto savedBatch=batch;
        header.flags=1;layer={};layer.opacity=1;layer.roughness=1;layer.normalStrength=1;
        layer.detailScaleX=layer.detailScaleY=1;layer.emissiveNits=1;
        layer.emissive[0]=4;layer.emissive[1]=2;layer.emissive[2]=1;
        batch.flags=BatchDoubleSided;publish();const auto highlight=center(draw(renderer,device));
        const auto expected=vehicleDisplaySrgb({4,2,1});
        for(unsigned channel=0;channel<3;channel++){
            const auto observed=int(highlight>>(16-8*channel)&255);
            check(std::abs(observed-int(std::lround(expected[channel]*255)))<=2,"HDR source color bypassed display tone mapping");
            check(observed<255,"HDR highlight clipped a source color channel to white");
        }
        header.flags=1|2|(1u<<8);batch.flags=BatchDoubleSided|BatchTranslucent;layer.opacity=.25f;
        publish();const auto translucent=center(draw(renderer,device));
        for(unsigned channel=0;channel<3;channel++)
            check(std::abs(int(translucent>>(16-8*channel)&255)-int(std::lround(expected[channel]*255*.25)))<=2,"display transform changed source material opacity");
        header=savedHeader;layer=savedLayer;batch=savedBatch;publish();
    }
    const auto original=draw(renderer,device);auto capturedBatch=batch;
    const VehicleQuaternion captured=vehicleQuaternionProduct({std::sqrt(.5f),0,0,std::sqrt(.5f)},{0,0,std::sin(.3f),std::cos(.3f)});
    for(auto& vertex:capturedBatch.vertices){const auto p=vehicleRotate(captured,{vertex.x,vertex.y,vertex.z}),n=vehicleRotate(captured,{vertex.nx,vertex.ny,vertex.nz});vertex.x=float(p.x);vertex.y=float(p.y);vertex.z=float(p.z);vertex.nx=float(n.x);vertex.ny=float(n.y);vertex.nz=float(n.z);}
    check(renderer.setVehicleFrame({capturedBatch},captured,1.1,1),"captured full-orientation frame");renderer.setVehiclePose({0,0,-4},{},1);auto corrected=draw(renderer,device);
    check(corrected.visible==original.visible,"captured quaternion delta changed restored triangle footprint");
    for(unsigned shift:{0U,8U,16U})check(std::abs(int(center(corrected)>>shift&255)-int(center(original)>>shift&255))<=1,"full-orientation delta did not restore source normal/lighting");
    header.flags=1|2|(5u<<8);batch.flags=BatchDoubleSided|BatchTranslucent;layer.opacity=.2f;publish();check(draw(renderer,device).visible>100,"PreMulAlpha glass lost reflection");
    renderer.onDeviceLost();minecraft.onDeviceLost();hr(device->Reset(&parameters),"compact material Reset");check(draw(renderer,device).visible>100,"compact maps/shaders did not survive Reset source recreation");
    check(renderer.resourceUsage().gpuMaterialCount>0,"material retirement fixture lacks compiled source shader");
    renderer.clear();check(!renderer.hasAvatar(),"disconnect retained vehicle material geometry");
    check(renderer.resourceUsage().cpuTextureCapacityBytes==0,"material clear retained compact map capacity");
    renderer.retirePendingResources();check(renderer.resourceUsage().gpuMaterialCount==0&&renderer.resourceUsage().gpuTextureCount==0,"idle menu retained compiled material shaders or compact GPU maps");
    std::puts("PASS: actual material schema/UV1 paint, separate opacity, BlendOp None/PreMulAlpha, translucent Z-write and BlackSky reflection, compact R8/RG8 and sRGB maps, normal lighting/reflection, Reset and disconnect; Minecraft formats unchanged.");
}
void atlasChecks(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters){
    Renderer reference,vehicle(true,true);
    constexpr uint32_t side=1024;
    std::vector<uint8_t> first(side*side*4),second(side*side*4);
    for(uint32_t y=0;y<side;y++)for(uint32_t x=0;x<side;x++){
        const size_t at=(size_t(y)*side+x)*4;
        first[at]=uint8_t(17*x+3*y);first[at+1]=uint8_t(5*x+11*y);first[at+2]=uint8_t(7*x+13*y);first[at+3]=uint8_t(19*x+23*y);
        second[at]=uint8_t(251-11*x-y);second[at+1]=uint8_t(191+3*x+7*y);second[at+2]=uint8_t(131+13*x+5*y);second[at+3]=uint8_t(47+17*x+29*y);
    }
    auto upload=[&](){for(Renderer* renderer:{&reference,&vehicle}){
        check(renderer->uploadTexture(31,side,side,first.data(),first.size()),"atlas first source upload");
        check(renderer->uploadTexture(32,side,side,second.data(),second.size()),"atlas second source upload");
    }};
    upload();
    auto verify=[&](float u,float v,uint32_t extraFlags=0){
        std::vector<RenderBatch> batches;
        for(unsigned material=0;material<3;material++){
            RenderBatch batch{material==2?0U:31U+material,BatchTranslucent|((material==1)?BatchDoubleSided:0U)|extraFlags,{}};
            // Six layers alternate source materials in the exact global sort.
            // Grouping them by material would visibly change these alpha sums.
            for(float z:{-.3f+.1f*material,.3f-.1f*material}){
                const uint32_t color=material==2?0x803f9bdf:0xc0ffffff;
                batch.vertices.insert(batch.vertices.end(),{{-.8f,-.8f,z,0,0,1,u,v,color},{.8f,-.8f,z,0,0,1,u,v,color},{0,.8f,z,0,0,1,u,v,color}});
            }
            batches.push_back(std::move(batch));
        }
        check(reference.setMesh(1,batches)&&vehicle.setMesh(1,std::move(batches)),"atlas exact-order fixture mesh");
        const auto original=draw(reference,device),packed=draw(vehicle,device);
        check(original.pixels.size()==packed.pixels.size(),"atlas fixture image sizes");
        for(size_t pixel=0;pixel<original.pixels.size();pixel++)
            check((original.pixels[pixel]&0xffffff)==(packed.pixels[pixel]&0xffffff),"atlas changed source RGB/alpha or global triangle order");
    };
    // Interior and clamped endpoints must sample the exact source texels.
    verify((7.5f)/side,(11.5f)/side);verify(0,0);verify(1,1);
    for(size_t at=0;at<first.size();at+=4){first[at]^=255;first[at+3]^=127;}
    upload();verify((7.5f)/side,(11.5f)/side);
    // Unbounded source UV and particle depth semantics retain the original
    // sorted material path; neither is altered to make an atlas eligible.
    verify(-.25f,1.25f);verify((7.5f)/side,(11.5f)/side,BatchParticle);
    reference.onDeviceLost();vehicle.onDeviceLost();hr(device->Reset(&parameters),"atlas actual device Reset");
    verify((7.5f)/side,(11.5f)/side);
    vehicle.clear();upload();verify((7.5f)/side,(11.5f)/side);
    std::puts("PASS: car translucent atlas matched original source RGB/alpha and global order across interior/endpoints, texture revisions, fallback UV/depth semantics, Reset and disconnect clear.");
}
void deformationChecks(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters,bool indexed=false){
    Renderer renderer(true),oracle(true);
    VehicleMaterial header{};header.epoch=1;header.id=1;header.layerCount=1;header.flags=1;
    VehicleMaterialLayer layer{};for(float& c:layer.baseColor)c=1;layer.opacity=1;layer.roughness=.6f;layer.ao=1;layer.emissive[0]=.2f;layer.emissive[1]=.4f;layer.emissive[2]=.8f;layer.emissiveNits=1;
    check(renderer.setVehicleMaterial(header,{layer})&&oracle.setVehicleMaterial(header,{layer}),"deformation fixture material");
    RenderBatch local{0,1,{{-.7f,-.3f,0,0,0,1,0,0,0xffffffff},{.9f,-.2f,0,0,0,1,1,0,0xffffffff},{-.1f,.6f,0,0,0,1,0,1,0xffffffff}},1,{{0,0},{1,0},{0,1}},{0,1,2}};
    if(indexed){local.indices=local.sourceIndices;local.sourceIndices.clear();}
    auto rotateBatch=[](RenderBatch b,VehicleQuaternion q,VehicleVector center=VehicleVector{}){for(auto& v:b.vertices){const auto p=vehicleRotate(q,{v.x,v.y,v.z}),n=vehicleRotate(q,{v.nx,v.ny,v.nz});v.x=float(p.x+center.x);v.y=float(p.y+center.y);v.z=float(p.z+center.z);v.nx=float(n.x);v.ny=float(n.y);v.nz=float(n.z);}return b;};
    auto zTurn=[](double degrees){double a=degrees*3.14159265358979323846/360.;return VehicleQuaternion{0,0,float(std::sin(a)),float(std::cos(a))};};
    auto yTurn=[](double degrees){double a=degrees*3.14159265358979323846/360.;return VehicleQuaternion{0,float(std::sin(a)),0,float(std::cos(a))};};
    // Initialize the actual device first: device change deliberately discards
    // deformation history before accepting fresh compatible endpoints.
    check(renderer.setVehicleFrame({local},{},.5,1,1,500),"initial deformation fixture");renderer.setVehiclePose({0,0,-4},{},1,1,.5);draw(renderer,device,1,camera(),placement(),0xff000000,500);
    auto before=rotateBatch(local,yTurn(90));auto after=rotateBatch(rotateBatch(local,zTurn(100),{.06,.03,0}),yTurn(180));
    check(renderer.setVehicleFrame({before},yTurn(90),1,1,1,1000),"actual inverse capture from");
    check(renderer.setVehicleFrame({after},yTurn(180),1.1,1,1,1100),"actual inverse capture to");
    renderer.setVehiclePose({0,0,-4},{},1,1,1.1);
    // The observed rigid fit rotates about the part's captured centroid and
    // linearly interpolates the two observed centers. An asymmetric triangle
    // has a nonzero centroid, so rotating its center around an unobserved
    // origin would prescribe a different (invented) intermediate trajectory.
    const VehicleVector fromCenter{(.9-.7-.1)/3.,(-.3-.2+.6)/3.,0};
    const auto turnedCenter=vehicleRotate(zTurn(100),fromCenter);
    const VehicleVector toCenter{turnedCenter.x+.06,turnedCenter.y+.03,turnedCenter.z};
    size_t imageDifferences{};Image analyticMidpoint;
    for(double fraction:{0.,.25,.5,.75}){auto actual=draw(renderer,device,1,camera(),placement(),0xff000000,1112+fraction*100);
        auto expected=local;const auto center=vehicleLerp(fromCenter,toCenter,fraction);
        for(auto& v:expected.vertices){const auto offset=vehicleRotate(zTurn(100*fraction),{v.x-fromCenter.x,v.y-fromCenter.y,v.z-fromCenter.z}),normal=vehicleRotate(zTurn(100*fraction),{v.nx,v.ny,v.nz});
            v.x=float(center.x+offset.x);v.y=float(center.y+offset.y);v.z=float(center.z+offset.z);v.nx=float(normal.x);v.ny=float(normal.y);v.nz=float(normal.z);}
        check(oracle.setVehicleFrame({expected},{},1,1),"independent analytic rigid centroid oracle");oracle.setVehiclePose({0,0,-4},{},1);
        const auto reference=draw(oracle,device);size_t difference{};for(size_t n=0;n<actual.pixels.size();++n)difference+=actual.pixels[n]!=reference.pixels[n];
        std::printf("Deformation analytic diagnostic fraction%.2f differingPixels%zu visibleActual%zu visibleOracle%zu\n",fraction,difference,actual.visible,reference.visible);
        if(difference>8){bmp(actual,"build\\beamng-render-tests\\deformation-analytic-rejected.bmp");bmp(reference,"build\\beamng-render-tests\\deformation-analytic-oracle.bmp");std::fprintf(stderr,"%s\n",renderer.describeFrame(device,camera(),placement()).c_str());}
        check(difference<=8,"GPU fitted rotation/residual/capture-basis changed analytic fixture pixels");check(actual.visible>500,"spinning shape collapsed between source captures");imageDifferences+=difference;
        if(fraction==.5){bmp(actual,"build\\beamng-render-tests\\deformation-rotation-midpoint.bmp");analyticMidpoint=actual;}}
    auto endpoint=draw(renderer,device,1,camera(),placement(),0xff000000,1212);check(oracle.setVehicleFrame({rotateBatch(local,zTurn(100),{.06,.03,0})},{},1,1),"final endpoint oracle");oracle.setVehiclePose({0,0,-4},{},1);
    auto endpointOracle=draw(oracle,device);size_t endpointDifference{};for(size_t n=0;n<endpoint.pixels.size();++n)endpointDifference+=endpoint.pixels[n]!=endpointOracle.pixels[n];check(endpointDifference<=8,"final captured endpoint changed");
    renderer.clear();check(renderer.setVehicleMaterial(header,{layer}),"new session material before idle retirement");
    check(renderer.setVehicleFrame({before},yTurn(90),1,1,1,1000)&&renderer.setVehicleFrame({after},yTurn(180),1.1,1,1,1100),"new session observed endpoints before idle retirement");renderer.setVehiclePose({0,0,-4},{},1,1,1.1);
    renderer.retirePendingResources();const auto renewedMidpoint=draw(renderer,device,1,camera(),placement(),0xff000000,1162);
    check(renewedMidpoint.pixels==analyticMidpoint.pixels,"previous GPU retirement discarded newer observed deformation endpoints");
    check(renderer.setVehicleFrame({before},yTurn(90),2,1,1,2000)&&renderer.setVehicleFrame({after},yTurn(180),2.1,1,1,2100),"same-ID repair fixture endpoints");
    auto preRepair=draw(renderer,device,1,camera(),placement(),0xff000000,2162);check(preRepair.pixels!=endpoint.pixels,"repair fixture did not exercise active deformation");
    renderer.resetVehicleDeformation();
    auto repaired=draw(renderer,device,1,camera(),placement(),0xff000000,2162);check(repaired.pixels==endpoint.pixels,"same-ID repair retained interpolation or lost actual geometry/materials");
    renderer.onDeviceLost();oracle.onDeviceLost();hr(device->Reset(&parameters),"deformation actual Reset");auto reset=draw(renderer,device,1,camera(),placement(),0xff000000,1212);check(reset.pixels==endpoint.pixels,"Reset retained stale deformation or changed actual endpoint");
    // Global alpha order must use the displayed interpolated surfaces,
    // rather than the latest (opposite) depth order.
    Renderer transparent(true),alphaOracle(true);
    header.flags=3|0x100;layer.opacity=.5f;layer.emissive[0]=1;layer.emissive[1]=layer.emissive[2]=0;
    check(transparent.setVehicleMaterial(header,{layer})&&alphaOracle.setVehicleMaterial(header,{layer}),"red glass material");
    header.id=2;layer.emissive[0]=0;layer.emissive[2]=1;check(transparent.setVehicleMaterial(header,{layer})&&alphaOracle.setVehicleMaterial(header,{layer}),"blue glass material");
    auto red=local,blue=local;red.flags=blue.flags=3;blue.materialId=2;
    auto depthBatch=[](RenderBatch b,float z){for(auto& v:b.vertices)v.z=z;return b;};
    std::vector<RenderBatch> old{depthBatch(red,.2f),depthBatch(blue,-.2f)},fresh{depthBatch(red,-.6f),depthBatch(blue,.3f)};
    check(transparent.setVehicleFrame(old,{},.5,1,1,500),"alpha initialize");transparent.setVehiclePose({0,0,-4},{},1,1,.5);draw(transparent,device,1,camera(),placement(),0xff000000,500);
    check(transparent.setVehicleFrame(old,{},1,1,1,1000)&&transparent.setVehicleFrame(fresh,{},1.1,1,1,1100),"alpha complete endpoints");transparent.setVehiclePose({0,0,-4},{},1,1,1.1);
    auto alpha=draw(transparent,device,1,camera(),placement(),0xff000000,1137);
    check(alphaOracle.setVehicleFrame({depthBatch(red,0),depthBatch(blue,-.075f)},{},1,1),"alpha independent interpolated depth oracle");alphaOracle.setVehiclePose({0,0,-4},{},1);
    auto expectedAlpha=draw(alphaOracle,device);check(alpha.pixels==expectedAlpha.pixels,"global alpha sorting used old/latest instead of displayed geometry");
    std::printf("PASS: %s GPU observed100-degree part rotation, inverse captured body bases, independent analytic midpoint (%zu boundary quantization pixels), exact actual endpoint/Reset and interpolated global-alpha depth order.\n",indexed?"indexed":"expanded",imageDifferences);
}
void indexedCapacityChecks(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters){
    // Synthetic lossless indexed quad and an independently expanded oracle.
    Renderer indexed(true),expanded(true),minecraft;
    VehicleMaterial h{};h.epoch=1;h.id=1;h.layerCount=1;h.flags=1;
    VehicleMaterialLayer l{};for(float& c:l.baseColor)c=1;l.opacity=l.ao=1;l.roughness=.6f;l.emissive[1]=.4f;l.emissiveNits=1;
    check(indexed.setVehicleMaterial(h,{l})&&expanded.setVehicleMaterial(h,{l}),"indexed capacity material");
    RenderBatch palette{0,1,{{-.7f,-.7f,0,0,0,1,0,1,0xff7faaff},{.7f,-.7f,0,0,0,1,1,1,0xff7faaff},{-.7f,.7f,0,0,0,1,0,0,0xff7faaff},{.7f,.7f,0,0,0,1,1,0,0xff7faaff}},1,{{0,1},{1,1},{0,0},{1,0}},{},{2,0,1,2,1,3}};
    RenderBatch oracle;oracle.materialId=1;oracle.flags=1;
    for(auto i:palette.indices){oracle.vertices.push_back(palette.vertices[i]);oracle.secondaryUV.push_back(palette.secondaryUV[i]);}
    check(!minecraft.setMesh(1,{palette}),"Minecraft accepted vehicle indexed representation");
    auto malformed=palette;malformed.indices[0]=4;check(!indexed.setVehicleFrame({malformed},{},1,1),"out-of-palette indexed triangle accepted");
    malformed=palette;malformed.indices.pop_back();check(!indexed.setVehicleFrame({malformed},{},1,1),"incomplete indexed triangle accepted");
    check(indexed.setVehicleFrame({palette},{},1,1)&&expanded.setVehicleFrame({oracle},{},1,1),"indexed quad/oracle ingest");
    indexed.setVehiclePose({0,0,-4},{},1);expanded.setVehiclePose({0,0,-4},{},1);
    const auto reference=draw(expanded,device);
    IDirect3DIndexBuffer9* nativeIndices{};hr(device->CreateIndexBuffer(12,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&nativeIndices,nullptr),"native-owned index fixture");
    hr(device->SetIndices(nativeIndices),"bind native-owned index fixture");
    const auto actual=draw(indexed,device);check(actual.pixels==reference.pixels,"indexed UV2/color/triangle order changed GPU pixels");
    IDirect3DIndexBuffer9* restored{};hr(device->GetIndices(&restored),"read restored native indices");
    const bool sameIndices=restored==nativeIndices;if(restored)restored->Release();
    hr(device->SetIndices(nullptr),"release native fixture binding");nativeIndices->Release();
    check(sameIndices,"indexed bridge draw changed native index-buffer binding");
    // Exercise the maximum index capacity without costly overlapping fill.
    // Synthetic degenerate triangles precede one actual visible quad.
    auto large=palette;large.indices.assign(MaxVehicleIndices,0);std::copy(palette.indices.begin(),palette.indices.end(),large.indices.end()-palette.indices.size());
    check(indexed.setVehicleFrame({large},{},2,1),"maximum complete vehicle index capacity rejected");
    check(draw(indexed,device).pixels==reference.pixels,"large index stream lost final triangles or palette vertices");
    large.indices.insert(large.indices.end(),{0,1,2});check(!indexed.setVehicleFrame({large},{},3,1),"unbounded vehicle index capacity accepted");
    std::vector<RenderBatch> batches(300,palette);check(indexed.setVehicleFrame(batches,{},3,1),"stock-like batch count above256 rejected");
    check(draw(indexed,device).pixels==reference.pixels,"indexed batches above256 changed geometry");
    indexed.onDeviceLost();expanded.onDeviceLost();hr(device->Reset(&parameters),"indexed actual Reset");
    check(draw(indexed,device).pixels==reference.pixels,"indexed buffers not restored after Reset");
    std::puts("PASS: compact indexed palettes preserve UV2/color/winding, reject malformed indices and Minecraft use, render1.5million indices/300batches, enforce the cap and recreate actual index buffers after Reset.");
}
struct SourceBounds {Vec3 minimum{},maximum{};bool valid{};};
SourceBounds bounds(const std::vector<RenderBatch>& batches){
    SourceBounds result{};for(const auto& batch:batches)for(const auto& v:batch.vertices){
        if(!result.valid){result.minimum=result.maximum={v.x,v.y,v.z};result.valid=true;}
        else {result.minimum.x=std::min(result.minimum.x,v.x);result.minimum.y=std::min(result.minimum.y,v.y);result.minimum.z=std::min(result.minimum.z,v.z);result.maximum.x=std::max(result.maximum.x,v.x);result.maximum.y=std::max(result.maximum.y,v.y);result.maximum.z=std::max(result.maximum.z,v.z);}}
    return result;
}
void replayActual(Renderer& renderer,IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters,const char* path,bool materialFixture=false,const char* captureLabel="Covet"){
    // The complete production decoder assembles real sequential texture chunks.
    // No network/game process is started and no source vertices are authored here.
    Transport peer({},true,true);std::ifstream in(path,std::ios::binary);check(bool(in),"open actual BeamNG framed capture");
    size_t textureCount{},meshFrames{},vertices{},frames{};SourceBounds last{};const size_t initialMemory=privateBytes();
    std::vector<RenderBatch> retainedSurface;VehicleMeshCapture retainedCapture{};uint32_t retainedEpoch{};
    while(true){Header h{};in.read(reinterpret_cast<char*>(&h),sizeof(h));if(in.eof()&&in.gcount()==0)break;
        check(in.gcount()==sizeof(h)&&h.magic==Magic&&h.version==ProtocolVersion&&h.size<=MaxPayload,"actual capture header");check(++frames<=65536,"actual capture excessive frame count");
        std::vector<uint8_t> payload(h.size);in.read(reinterpret_cast<char*>(payload.data()),static_cast<std::streamsize>(payload.size()));check(bool(in),"actual capture truncated payload");
        bool guestPacket=h.type==Message::Texture||h.type==Message::TextureChunk||h.type==Message::Mesh||h.type==Message::VehicleMeshChunk||h.type==Message::State||h.type==Message::Motion||h.type==Message::VehicleInfo||h.type==Message::VehicleSurfaceChunk||h.type==Message::VehicleTextureChunk||h.type==Message::VehicleMaterial||h.type==Message::VehiclePose||h.type==Message::VehicleDashboard;
        if(!guestPacket)continue;
        if(!peer.accept(h.type,payload)){
            std::fprintf(stderr,"Rejected actual packet type%u sequence%u size%u frameIndex%zu\n",unsigned(h.type),h.sequence,h.size,frames);
            if(h.type==Message::VehicleMeshChunk&&payload.size()>=sizeof(VehicleMeshChunk)){
                VehicleMeshChunk chunk{};std::memcpy(&chunk,payload.data(),sizeof(chunk));
                std::fprintf(stderr,"Car chunk epoch%u frame%u total%u offset%u count%u reserved%u\n",chunk.epoch,chunk.frame,chunk.totalBytes,chunk.offset,chunk.byteCount,chunk.reserved);
            }
            throw std::runtime_error("production decoder rejected actual captured packet");
        }
        auto updates=peer.takeUpdates();for(auto& tex:updates.textures){check(renderer.uploadVehicleTexture(tex.id,tex.width,tex.height,std::move(tex.rgba),tex.format,tex.compressed),"production renderer rejected actual full texture");++textureCount;}
        for(const auto& material:updates.vehicleMaterials)check(renderer.setVehicleMaterial(material.header,material.layers),"actual material descriptor rejected");
        if(updates.newMesh){std::vector<RenderBatch> source;for(auto& batch:updates.mesh)source.push_back({batch.textureId,batch.flags,std::move(batch.vertices),batch.materialId,std::move(batch.secondaryUV),std::move(batch.sourceIndices),std::move(batch.indices)});
            last=bounds(source);vertices=0;for(const auto& batch:source)vertices+=batch.vertices.size();
            if(updates.surfaceMesh){retainedSurface=source;retainedCapture=updates.meshCapture;retainedEpoch=updates.meshHeader.epoch;const auto& q=updates.meshCapture.quaternion;VehicleQuaternion capture{q[0],q[1],q[2],q[3]};check(renderer.setVehicleFrame(std::move(source),capture,updates.meshCapture.sourceSeconds,updates.meshHeader.epoch),"actual surface frame rejected");renderer.setVehiclePose({},capture,updates.meshHeader.epoch);}
            else check(renderer.setMesh(1,std::move(source)),"production renderer rejected real Covet source mesh");++meshFrames;}
    }
    check(textureCount&&meshFrames&&vertices&&last.valid&&renderer.hasAvatar(),"actual Covet capture is missing source mesh/full textures");
    const Vec3 center{(last.minimum.x+last.maximum.x)*.5f,(last.minimum.y+last.maximum.y)*.5f,(last.minimum.z+last.maximum.z)*.5f};
    const double dx=double(last.maximum.x)-last.minimum.x,dy=double(last.maximum.y)-last.minimum.y,dz=double(last.maximum.z)-last.minimum.z;
    const float radius=static_cast<float>(.5*std::sqrt(dx*dx+dy*dy+dz*dz));
    check(std::isfinite(radius)&&radius>0&&radius<128,"actual Covet source bounds invalid");
    auto native=camera();constexpr float c=.766044443f,s=.64278761f;native.view[0][0]=c;native.view[0][2]=-s;native.view[0][3]=-c*center.x+s*center.z;
    native.view[1][3]=-center.y;native.view[2][0]=s;native.view[2][2]=c;native.view[2][3]=-s*center.x-c*center.z-radius*3.5f;
    native.projection[2][2]=-.1f/999.9f;native.projection[2][3]=-100.f/999.9f;
    RenderPlacement p{};p.worldUnitsPerBlock=1;p.drawScene=p.drawDynamic=false;
    auto coldStart=std::chrono::steady_clock::now();auto image=draw(renderer,device,1,native,p);const double coldMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-coldStart).count();
    const std::string sourceImage=std::string("build\\beamng-render-tests\\source-")+captureLabel+".bmp";
    check(image.visible>100,"source/material fixture mesh produced no GPU pixels");bmp(image,materialFixture?"build\\beamng-render-tests\\installed-materials-fixture.bmp":sourceImage.c_str());
    size_t triangleCorners{};for(const auto& batch:retainedSurface)triangleCorners+=batch.indices.empty()?batch.vertices.size():batch.indices.size();
    if(!retainedSurface.empty()&&retainedSurface.size()<=MaxBatches&&triangleCorners<=MaxVertices){
        std::vector<RenderBatch> oracle;oracle.reserve(retainedSurface.size());
        for(const auto& source:retainedSurface){RenderBatch expanded;expanded.textureId=source.textureId;expanded.flags=source.flags;expanded.materialId=source.materialId;
            const size_t count=source.indices.empty()?source.vertices.size():source.indices.size();expanded.vertices.reserve(count);expanded.secondaryUV.reserve(count);
            for(size_t i=0;i<count;++i){const size_t index=source.indices.empty()?i:source.indices[i];expanded.vertices.push_back(source.vertices[index]);expanded.secondaryUV.push_back(source.secondaryUV[index]);}
            oracle.push_back(std::move(expanded));}
        const auto& q=retainedCapture.quaternion;const VehicleQuaternion captured{q[0],q[1],q[2],q[3]};
        check(renderer.setVehicleFrame(std::move(oracle),captured,retainedCapture.sourceSeconds,retainedEpoch),"independently expanded genuine-source oracle rejected");
        const auto expandedImage=draw(renderer,device,1,native,p);check(expandedImage.pixels==image.pixels,"indexed actual source lost triangles, UV2, material or alpha order versus expanded oracle");
        check(renderer.setVehicleFrame(std::move(retainedSurface),captured,retainedCapture.sourceSeconds,retainedEpoch),"restore genuine indexed palette");
        std::printf("PASS: genuine source indexed palette %zu vertices / %zu triangle indices exactly matches all expanded-oracle GPU pixels.\n",vertices,triangleCorners);
    }
    if(materialFixture)check(textureCount==79&&vertices==75,"installed material fixture lost maps or test batches");
    const std::string sourceName=materialFixture?"installed actual79-map/25-material fixture (synthetic triangles and paint constants)":std::string("actual captured ")+captureLabel;
    std::printf("PASS: %s via production Transport/Renderer: %zu source textures, %zu mesh frames, %zu vertices, %zu visible pixels.\n",sourceName.c_str(),textureCount,meshFrames,vertices,image.visible);
    std::printf("%s\n",renderer.describeFrame(device,native,p).c_str());
    const auto residency=renderer.resourceUsage();
    check(residency.cpuTextureCapacityBytes==residency.cpuTextureBytes&&residency.cpuTextureBytes<=MaxVehicleTextureStorageBytes,"actual capture retained oversized or out-of-budget storage");
    std::printf("Production compressed residency: decoded%zu retained%zu capacity%zu bytes; full original map dimensions/channels.\n",residency.decodedTextureBytes,residency.cpuTextureBytes,residency.cpuTextureCapacityBytes);
    std::printf("Cold resource upload/shader compilation/readback %.3fms; process private bytes before%zu after%zu.\n",coldMs,initialMemory,privateBytes());
    // A warm production-render component measurement. Each readback completes
    // the prior GPU work, but its cost is excluded from the reported CPU time.
    std::vector<double> renderTimes;
    for(unsigned sample=0;sample<60;++sample){
        hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"benchmark clear");
        hr(device->BeginScene(),"benchmark begin");auto begin=std::chrono::steady_clock::now();
        bool valid{};check(renderer.render(device,native,p,&valid)&&valid,"benchmark actual source draw");
        auto end=std::chrono::steady_clock::now();hr(device->EndScene(),"benchmark end");readback(device);
        renderTimes.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
    }
    std::sort(renderTimes.begin(),renderTimes.end());
    std::printf("%s warm renderer CPU: median%.3fms p95%.3fms max%.3fms; 60 samples, GPU completion between samples. This is not live RE4 FPS.\n",sourceName.c_str(),renderTimes[30],renderTimes[56],renderTimes.back());
    if(materialFixture){renderer.onDeviceLost();hr(device->Reset(&parameters),"full actual79-map GPU Reset");auto reset=draw(renderer,device,1,native,p);check(reset.pixels==image.pixels,"actual79-map/shader recreation changed fixture pixels");std::puts("PASS: all79 full-size compact source maps and25 shader permutations recreated after actual Reset with exact fixture RGB.");}
}
void originalCatalogChecks(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters,const char* path){
    // Original installed maps, synthetic single-texel triangles. This private
    // offline fixture proves residency and GPU storage, not a selected car's
    // body geometry, physics, live game rendering or sustained frame rate.
    struct Map {uint32_t id{},width{},height{},format{},bytes{};};
    struct Sample {uint32_t x{},y{};uint8_t rgba[4]{};};
    struct Record {Map map;std::array<Sample,7> samples;};
    std::ifstream input(path,std::ios::binary);check(bool(input),"open original texture catalog fixture");
    auto read=[&](void* out,size_t size){input.read(static_cast<char*>(out),static_cast<std::streamsize>(size));check(bool(input),"truncated original catalog fixture");};
    uint32_t magic{},count{};read(&magic,4);read(&count,4);check(magic==0x51415443&&count&&count<=256,"original catalog metadata bound");
    Transport receiver({},true,true);Renderer renderer(true);std::vector<Record> records;size_t logical{},packedMaps{};
    const auto initial=privateBytes();
    for(uint32_t index=0;index<count;++index){
        Record record{};read(&record.map,sizeof(record.map));read(record.samples.data(),sizeof(record.samples));const auto& m=record.map;
        check(m.id&&m.width&&m.height&&m.width<=4096&&m.height<=4096&&(m.format==1||m.format==2||m.format==4||m.format==260)&&m.bytes==size_t(m.width)*m.height*(m.format&255)&&m.bytes<=MaxVehicleTextureMapBytes,"original catalog map dimensions/channels");
        for(const auto& sample:record.samples)check(sample.x<m.width&&sample.y<m.height,"original catalog sample outside source map");
        logical+=m.bytes;check(logical<=MaxVehicleDecodedTextureBytes,"original catalog logical bound");
        for(uint32_t offset=0;offset<m.bytes;){const uint32_t bytes=std::min<uint32_t>(65536,m.bytes-offset);
            VehicleTextureChunk chunk{m.id,m.width,m.height,offset,bytes,m.format,0};std::vector<uint8_t> payload(sizeof(chunk)+bytes);
            std::memcpy(payload.data(),&chunk,sizeof(chunk));read(payload.data()+sizeof(chunk),bytes);
            check(receiver.accept(Message::VehicleTextureChunk,payload),"production receiver rejected original catalog map");offset+=bytes;
        }
        auto updates=receiver.takeUpdates();check(updates.textures.size()==1,"original catalog map did not complete once");auto& texture=updates.textures.front();packedMaps+=texture.compressed;
        check(renderer.uploadVehicleTexture(texture.id,texture.width,texture.height,std::move(texture.rgba),texture.format,texture.compressed),"renderer rejected complete original catalog map");
        check(texture.rgba.empty()&&texture.rgba.capacity()==0,"catalog ownership transfer retained source storage");records.push_back(record);
    }
    check(input.peek()==std::ifstream::traits_type::eof(),"original catalog trailing bytes");
    auto usage=renderer.resourceUsage();check(logical>384u*1024u*1024u&&usage.decodedTextureBytes==logical,"catalog did not reproduce former384MiB failure");
    check(packedMaps&&usage.cpuTextureBytes<logical&&usage.cpuTextureCapacityBytes==usage.cpuTextureBytes&&usage.cpuTextureBytes<=MaxVehicleTextureStorageBytes,"catalog retained a full decoded CPU copy or exceeded storage bound");
    std::vector<uint32_t> beforeReset;
    auto sampleCatalog=[&](bool compare){
        size_t at{};for(const auto& record:records){const auto& m=record.map;
            for(const auto& sample:record.samples){const float u=(float(sample.x)+.5f)/m.width,v=(float(sample.y)+.5f)/m.height;
                RenderBatch triangle{m.id,BatchDoubleSided,{{-1,-1,0,0,0,1,u,v,0xffffffff},{1,-1,0,0,0,1,u,v,0xffffffff},{0,1,0,0,0,1,u,v,0xffffffff}}};
                check(renderer.setMesh(1,{triangle}),"original catalog synthetic sampler geometry");const auto image=draw(renderer,device);const uint32_t observed=image.pixels[(Height/2)*Width+Width/2]&0xffffff;
                const uint32_t expected=sample.rgba[3]>127?uint32_t(sample.rgba[0])<<16|uint32_t(sample.rgba[1])<<8|sample.rgba[2]:0;
                check(observed==expected,"original catalog sampled texel changed source RGB/R8/RG8 or alpha discard");
                if(compare)check(at<beforeReset.size()&&observed==beforeReset[at],"original catalog Reset changed exact GPU texel");else beforeReset.push_back(observed);++at;
            }
        }
        check(at==records.size()*7,"original catalog lost requested samples");
    };
    sampleCatalog(false);usage=renderer.resourceUsage();check(usage.gpuTextureCount==count&&usage.cpuTextureCapacityBytes==usage.cpuTextureBytes,"catalog lost GPU maps or retained decoded upload scratch");
    renderer.onDeviceLost();hr(device->Reset(&parameters),"original catalog actual Reset");sampleCatalog(true);
    usage=renderer.resourceUsage();check(usage.gpuTextureCount==count&&usage.cpuTextureCapacityBytes==usage.cpuTextureBytes&&usage.cpuTextureBytes<=MaxVehicleTextureStorageBytes,"catalog Reset retained scratch or exceeded actual budget");
    std::printf("PASS: original installed catalog %u maps (%zu compressed): decoded%zu retained%zu capacity%zu bytes; %zu independent exact RGB/scalar/normal-plane samples and actual Reset; all original dimensions/channels preserved. Synthetic sampler triangles only, not vehicle geometry/live FPS. Private bytes before%zu after%zu.\n",count,packedMaps,logical,usage.cpuTextureBytes,usage.cpuTextureCapacityBytes,beforeReset.size(),initial,privateBytes());
    renderer.clear();renderer.retirePendingResources();usage=renderer.resourceUsage();check(!usage.cpuTextureCapacityBytes&&!usage.gpuTextureCount&&!usage.decodedTextureBytes,"original catalog retirement retained CPU/GPU maps");
}
struct DeformationSource{Mesh header{};VehicleMeshCapture capture{};std::vector<RenderBatch> batches;};
DeformationSource readDeformationSource(const char* path){
    std::ifstream in(path,std::ios::binary|std::ios::ate);check(bool(in),"open genuine deformation endpoint");
    const auto length=in.tellg();check(length>=std::streamoff(64)&&length<=std::streamoff(MaxVehicleSurfaceBytes),"bounded genuine deformation body");
    std::vector<uint8_t> body(static_cast<size_t>(length));in.seekg(0);in.read(reinterpret_cast<char*>(body.data()),length);check(bool(in),"read exact source body");
    Mesh header{};std::memcpy(&header,body.data(),sizeof(header));
    // Only reframing is synthesized here. The captured body, capture clock,
    // orientation, indexed vertices and all material references stay untouched
    // and pass through the unchanged strict production Surface13 decoder.
    Transport peer({},true);
    for(size_t offset=0;offset<body.size();offset+=65536){const size_t count=std::min<size_t>(65536,body.size()-offset);
        VehicleMeshChunk chunk{header.epoch,header.frame,uint32_t(body.size()),uint32_t(offset),uint32_t(count),0};
        std::vector<uint8_t> packet(sizeof(chunk)+count);std::memcpy(packet.data(),&chunk,sizeof(chunk));std::memcpy(packet.data()+sizeof(chunk),body.data()+offset,count);
        check(peer.accept(Message::VehicleSurfaceChunk,packet),"strict production decoder rejected genuine deformation endpoint");}
    auto updates=peer.takeUpdates();check(updates.newMesh&&updates.surfaceMesh,"complete deformation endpoint not published");
    DeformationSource result{updates.meshHeader,updates.meshCapture};
    for(auto& b:updates.mesh)result.batches.push_back({b.textureId,b.flags,std::move(b.vertices),b.materialId,std::move(b.secondaryUV),std::move(b.sourceIndices),std::move(b.indices)});
    return result;
}
void replayDeformation(Renderer& renderer,IDirect3DDevice9* device,const char* first,const char* second){
    auto from=readDeformationSource(first),to=readDeformationSource(second);
    const double gap=(to.capture.sourceSeconds-from.capture.sourceSeconds)*1000;
    check(from.header.epoch==to.header.epoch&&gap>0&&gap<250&&compatibleVehicleDeformation(from.batches,to.batches),"actual endpoints lack safe timing/topology continuity");
    auto bound=bounds(to.batches);check(bound.valid,"actual deformation bounds");const Vec3 center{(bound.minimum.x+bound.maximum.x)*.5f,(bound.minimum.y+bound.maximum.y)*.5f,(bound.minimum.z+bound.maximum.z)*.5f};
    const float radius=float(.5*std::sqrt(std::pow(bound.maximum.x-bound.minimum.x,2)+std::pow(bound.maximum.y-bound.minimum.y,2)+std::pow(bound.maximum.z-bound.minimum.z,2)));
    auto native=camera();constexpr float c=.766044443f,s=.64278761f;native.view[0][0]=c;native.view[0][2]=-s;native.view[0][3]=-c*center.x+s*center.z;
    native.view[1][3]=-center.y;native.view[2][0]=s;native.view[2][2]=c;native.view[2][3]=-s*center.x-c*center.z-radius*3.5f;
    native.projection[2][2]=-.1f/999.9f;native.projection[2][3]=-100.f/999.9f;
    RenderPlacement p{};p.worldUnitsPerBlock=1;p.drawScene=p.drawDynamic=false;
    const auto& qa=from.capture.quaternion;const auto& qb=to.capture.quaternion;VehicleQuaternion before{qa[0],qa[1],qa[2],qa[3]},after{qb[0],qb[1],qb[2],qb[3]};
    constexpr double arrival=1000000;std::vector<double> ingest;
    auto start=std::chrono::steady_clock::now();check(renderer.setVehicleFrame(std::move(from.batches),before,from.capture.sourceSeconds,from.header.epoch,1,arrival),"first actual deformation ingest");
    ingest.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    start=std::chrono::steady_clock::now();check(renderer.setVehicleFrame(std::move(to.batches),after,to.capture.sourceSeconds,to.header.epoch,1,arrival+gap),"second actual deformation ingest");
    ingest.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    renderer.setVehiclePose({},after,to.header.epoch,1,to.capture.sourceSeconds);
    const double delay=std::clamp(gap+12,VehicleDeformationClock::MinDelayMs,VehicleDeformationClock::MaxDelayMs),midpoint=arrival+delay+gap*.5;
    auto image=draw(renderer,device,1,native,p,0xff000000,midpoint);check(image.visible>100,"actual complete car morphology disappeared");bmp(image,"build\\beamng-render-tests\\source-covet-deformation-midpoint.bmp");
    std::vector<double> renderTimes;
    for(unsigned sample=0;sample<60;++sample){hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"deformation benchmark clear");hr(device->BeginScene(),"deformation benchmark begin");
        auto begin=std::chrono::steady_clock::now();bool valid{};check(renderer.render(device,native,p,&valid,midpoint)&&valid,"warm actual deformation draw");
        const auto end=std::chrono::steady_clock::now();hr(device->EndScene(),"deformation benchmark end");readback(device);renderTimes.push_back(std::chrono::duration<double,std::milli>(end-begin).count());}
    std::sort(renderTimes.begin(),renderTimes.end());
    std::printf("Actual Covet deformation warm renderer CPU median%.3fms p95%.3fms max%.3fms; actual pair%.3fms capture delta, ingest%.3f/%.3fms,60 samples excluding completion/readback. Controlled replay arrival/identity, unchanged real endpoints; this is not live RE4 FPS.\n",renderTimes[30],renderTimes[56],renderTimes.back(),gap,ingest[0],ingest[1]);
    std::printf("%s\n",renderer.describeFrame(device,native,p).c_str());
}
void originalHudReplay(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS& parameters,const char* path){
    std::ifstream in(path,std::ios::binary|std::ios::ate);check(bool(in),"open original Hud4 source capture");
    const auto length=in.tellg();check(length>=std::streamoff(sizeof(Hud))&&length<=std::streamoff(MaxPayload+sizeof(Header)),"bounded original Hud4 capture size");
    std::vector<uint8_t> payload(static_cast<size_t>(length));in.seekg(0);in.read(reinterpret_cast<char*>(payload.data()),length);check(bool(in),"read exact original Hud4 capture");
    uint32_t prefix{};std::memcpy(&prefix,payload.data(),sizeof(prefix));
    if(prefix==Magic){Header packet{};check(payload.size()>=sizeof(packet),"original Hud4 framed header");std::memcpy(&packet,payload.data(),sizeof(packet));
        check(packet.magic==Magic&&packet.version==ProtocolVersion&&packet.type==Message::Hud&&packet.size==payload.size()-sizeof(packet),"exact single original Hud4 framed packet");payload.erase(payload.begin(),payload.begin()+sizeof(packet));}
    Transport vehicle({},true);check(vehicle.accept(Message::Hud,payload),"production vehicle decoder rejected original Hud4 capture");
    auto updates=vehicle.takeUpdates();check(updates.newHud,"original Hud4 did not publish through production decoder");
    const auto source=updates.hud;check(source.width&&source.height&&source.rgba.size()==size_t(source.width)*source.height*4,"original Hud4 decoded dimensions/pixels");
    HudRenderer hud;hud.update(source);constexpr uint32_t background=0xff235f9b;
    auto renderOriginal=[&](){
        IDirect3DSurface9 *original{},*depth{},*target{};D3DVIEWPORT9 viewport{};
        hr(device->GetRenderTarget(0,&original),"original UI native target");device->GetDepthStencilSurface(&depth);hr(device->GetViewport(&viewport),"original UI native viewport");
        struct Restore {IDirect3DDevice9* device;IDirect3DSurface9 *original,*depth,*target;D3DVIEWPORT9 viewport;~Restore(){device->SetRenderTarget(0,original);device->SetDepthStencilSurface(depth);device->SetViewport(&viewport);if(target)target->Release();if(depth)depth->Release();if(original)original->Release();}} restore{device,original,depth,nullptr,viewport};
        hr(device->CreateRenderTarget(source.width,source.height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr),"original source-size UI target");restore.target=target;
        hr(device->SetDepthStencilSurface(nullptr),"UI target detach native depth");hr(device->SetRenderTarget(0,target),"bind original UI target");
        D3DVIEWPORT9 sourceViewport{0,0,source.width,source.height,0,1};hr(device->SetViewport(&sourceViewport),"source-size original UI viewport");
        hr(device->Clear(0,nullptr,D3DCLEAR_TARGET,background,1,0),"original UI blue oracle background");
        hr(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_DESTCOLOR),"UI native blend sentinel");
        hr(device->BeginScene(),"original UI begin");bool rendered=hud.render(device,false,0,0);hr(device->EndScene(),"original UI end");check(rendered,"native HudRenderer rejected original UI");
        DWORD blend{};hr(device->GetRenderState(D3DRS_SRCBLEND,&blend),"native UI blend restoration");check(blend==D3DBLEND_DESTCOLOR,"original UI changed retained native blend state");
        return readback(device);
    };
    auto image=renderOriginal();check(image.pixels.size()==size_t(source.width)*source.height,"original UI source-size readback");
    size_t transparent{},partial{};
    for(uint32_t y=0;y<source.height;++y)for(uint32_t x=0;x<source.width;++x){
        const uint32_t sourceY=(source.flags&1)?source.height-y-1:y;const size_t offset=(size_t(sourceY)*source.width+x)*4;
        const auto* pixel=source.rgba.data()+offset;const unsigned alpha=pixel[3];const auto actual=image.pixels[size_t(y)*source.width+x];
        transparent+=alpha==0;partial+=alpha>0&&alpha<255;
        for(unsigned channel=0;channel<3;++channel){const unsigned shift=16-channel*8,base=(background>>shift)&255;
            const int expected=int(pixel[channel])+int(std::lround(double(base)*(255-alpha)/255.));const int observed=int((actual>>shift)&255);
            check(std::abs(std::min(expected,255)-observed)<=1,"original UI changed premultiplied color/alpha, top-down rows or transparent background");}
    }
    check(transparent&&partial,"original source UI fixture lacks proven transparent/partial-alpha pixels");
    bmp(image,"build\\beamng-render-tests\\original-source-ui-native.bmp",source.width,source.height);
    hud.onDeviceLost();hr(device->Reset(&parameters),"original UI actual device Reset");const auto recreated=renderOriginal();
    check(recreated.pixels==image.pixels,"original UI Reset recreation changed source alpha/color");
    std::printf("PASS: genuine original UI Hud4 through production vehicle decoder/fullscreen HudRenderer: %ux%u, %zu transparent/%zu partial-alpha pixels; independent premultiplied blue-background oracle, retained state and exact Reset RGB. Source file: %s.\n",source.width,source.height,transparent,partial,path);
}
}

int main(int argc,char** argv){
    IDirect3D9* api{};IDirect3DDevice9* device{};HWND window{};
    try{
        const char* capture{};const char* captureLabel="Covet";const char* hudCapture{};const char* textureCatalog{};const char *deformationFrom{},*deformationTo{};bool materialFixture=false;DWORD vertexProcessing=D3DCREATE_SOFTWARE_VERTEXPROCESSING;bool atlasEnabled=true;
        for(int argument=1;argument<argc;argument++){
            if(std::strcmp(argv[argument],"--hardware-vp")==0)vertexProcessing=D3DCREATE_HARDWARE_VERTEXPROCESSING;
            else if(std::strcmp(argv[argument],"--no-atlas")==0)atlasEnabled=false;
            else if(std::strcmp(argv[argument],"--capture")==0&&argument+1<argc)capture=argv[++argument];
            else if(std::strcmp(argv[argument],"--capture-label")==0&&argument+1<argc){
                captureLabel=argv[++argument];const auto length=std::strlen(captureLabel);check(length&&length<=64,"capture label length");
                for(size_t i=0;i<length;++i){const char c=captureLabel[i];check((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_',"capture label contains unsafe filename characters");}
            }
            else if(std::strcmp(argv[argument],"--material-fixture")==0&&argument+1<argc){capture=argv[++argument];materialFixture=true;}
            else if(std::strcmp(argv[argument],"--hud-capture")==0&&argument+1<argc)hudCapture=argv[++argument];
            else if(std::strcmp(argv[argument],"--texture-catalog")==0&&argument+1<argc)textureCatalog=argv[++argument];
            else if(std::strcmp(argv[argument],"--deformation-pair")==0&&argument+2<argc){deformationFrom=argv[++argument];deformationTo=argv[++argument];}
            else throw std::runtime_error("usage: beamng_renderer_harness [--hardware-vp] [--no-atlas] [--capture actual-framed-covet.bin] [--deformation-pair from-surface-body.bin to-surface-body.bin] [--material-fixture fixture.bin] [--hud-capture original-Hud4-body.bin] [--texture-catalog original-map-fixture.bin]");
        }
        std::filesystem::create_directories("build\\beamng-render-tests");
        WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="RE4CraftVehicleAssetFixture";check(RegisterClassA(&wc)!=0,"register hidden fixture window");
        window=CreateWindowExA(0,wc.lpszClassName,"RE4Craft vehicle renderer fixture",WS_OVERLAPPEDWINDOW,0,0,Width,Height,nullptr,nullptr,wc.hInstance,nullptr);check(window!=nullptr,"hidden fixture window");
        api=Direct3DCreate9(D3D_SDK_VERSION);check(api!=nullptr,"D3D9 factory");
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=Width;pp.BackBufferHeight=Height;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,vertexProcessing,&pp,&device),"actual HAL device");
        D3DDEVICE_CREATION_PARAMETERS creation{};hr(device->GetCreationParameters(&creation),"actual creation flags");
        std::printf("Actual HAL device vertex processing: %s (creation flags0x%08lX).\n",(creation.BehaviorFlags&D3DCREATE_HARDWARE_VERTEXPROCESSING)?"hardware":"software",static_cast<unsigned long>(creation.BehaviorFlags));
        D3DCAPS9 caps{};hr(device->GetDeviceCaps(&caps),"actual texture caps");check(caps.MaxTextureWidth>=4096&&caps.MaxTextureHeight>=4096,"actual GPU cannot upload4096 RGBA texture");
        {
            Renderer minecraft,vehicle(true);std::vector<uint8_t> rgba(size_t(TextureSide)*TextureSide*4);
            for(UINT y=0;y<TextureSide;++y)for(UINT x=0;x<TextureSide;++x){auto color=sourceColor(x,y);size_t at=(size_t(y)*TextureSide+x)*4;rgba[at]=uint8_t(color>>16);rgba[at+1]=uint8_t(color>>8);rgba[at+2]=uint8_t(color);rgba[at+3]=uint8_t(color>>24);}
            const auto* completedPixels=rgba.data();const size_t completedBytes=rgba.size();
            check(!minecraft.uploadTexture(7,TextureSide,TextureSide,std::move(rgba)),"default Minecraft renderer accepted vehicle4k assets");check(rgba.data()==completedPixels&&rgba.size()==completedBytes,"rejected upload consumed packet ownership");
            check(!vehicle.uploadTexture(8,TextureSide,TextureSide,rgba.data(),rgba.size()-1),"truncated4k upload accepted");
            struct Fence {explicit Fence(size_t bytes){deniedAllocationSize=bytes;}~Fence(){deniedAllocationSize=0;}};
            {Fence fence(completedBytes);check(vehicle.uploadTexture(7,TextureSide,TextureSide,std::move(rgba)),"vehicle4k ownership transfer rejected");}
            check(rgba.empty()&&rgba.capacity()==0,"ownership transfer retained completed packet allocation");check(vehicle.resourceUsage().cpuTextureBytes==completedBytes&&vehicle.resourceUsage().cpuTextureCapacityBytes==completedBytes,"ownership transfer changed exact packet size");
            textureChecks(vehicle,device);alphaChecks(vehicle,device);std::puts("PASS: synthetic GPU triangles sampled seven exact RGB texels and sourcealpha through production translucent materials across a4096x4096 RGBA texture; default Minecraft4k rejection preserved.");
            check(vehicle.setMesh(1,{tile(3171,2297)}),"depth test mesh");auto occluded=draw(vehicle,device,0);check(!occluded.visible,"vehicle ignored retained native depth buffer");
            hr(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"native depth scene clear");hr(device->BeginScene(),"native depth begin");nativeWall(device);bool worldValid{};check(vehicle.render(device,camera(),placement(),&worldValid)&&worldValid,"vehicle at native depth pass");hr(device->EndScene(),"native depth end");
            auto image=readback(device);check((image.pixels[(Height/2)*Width+Width/2-20]&0xffffff)==0x123456,"4k mesh painted through native wall");check((image.pixels[(Height/2)*Width+Width/2+20]&0xffffff)==(sourceColor(3171,2297)&0xffffff),"visible4k surface changed beside native wall");bmp(image,"build\\beamng-render-tests\\synthetic-native-depth.bmp");
            vehicle.onDeviceLost();minecraft.onDeviceLost();hr(device->Reset(&pp),"actual device Reset");textureChecks(vehicle,device);
            vehicle.onDeviceLost();minecraft.onDeviceLost();device->Release();device=nullptr;hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,vertexProcessing,&pp,&device),"actual device recreation");textureChecks(vehicle,device);
            std::puts("PASS: native depth occlusion and exact4k reupload after actual Reset and device recreation.");
            idleRetirementChecks(vehicle,device,pp);
        }
        atlasChecks(device,pp);
        compressedStorageChecks(device,pp);
        vehicleMaterialChecks(device,pp);
        deformationChecks(device,pp);
        deformationChecks(device,pp,true);
        indexedCapacityChecks(device,pp);
        if(hudCapture)originalHudReplay(device,pp,hudCapture);
        if(textureCatalog)originalCatalogChecks(device,pp,textureCatalog);
        if(capture){std::printf("Car atlas enabled: %s.\n",atlasEnabled?"yes":"no");Renderer actual(true,atlasEnabled);replayActual(actual,device,pp,capture,materialFixture,captureLabel);if(deformationFrom)replayDeformation(actual,device,deformationFrom,deformationTo);}else {check(!deformationFrom,"deformation replay requires actual material/texture capture");std::puts("Actual vehicle replay skipped: no captured source file supplied. Synthetic triangles establish GPU asset/depth behavior only.");}
        device->Release();device=nullptr;api->Release();api=nullptr;DestroyWindow(window);window=nullptr;return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"BeamNG renderer fixture failed: %s\n",e.what());if(device)device->Release();if(api)api->Release();if(window)DestroyWindow(window);return 1;}
}
