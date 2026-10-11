#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d9.h>
#include "../native/renderer.hpp"
#include "../native/mesh_material.hpp"
#include <cstdio>
#include <stdexcept>
#include <cmath>
#include <cstring>
using namespace re4craft;
namespace {
constexpr UINT Size=128;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void hr(HRESULT value,const char* message){if(FAILED(value)){char line[256]{};std::snprintf(line,sizeof(line),"%s HRESULT0x%08lX",message,static_cast<unsigned long>(value));throw std::runtime_error(line);}}
NativeSnapshot camera(){NativeSnapshot s{};s.valid=s.alive=s.playing=true;s.view[0][0]=s.view[1][1]=s.view[2][2]=1;s.projection[0][0]=s.projection[1][1]=2.41421356f;s.projection[2][2]=-1.f/99;s.projection[2][3]=-100.f/99;s.projection[3][2]=-1;return s;}
RenderBatch quad(uint32_t texture,uint32_t flags,uint32_t color=0xffffffff,float z=0){return {texture,flags,{{-1,-1,z,0,0,1,0,1,color},{1,-1,z,0,0,1,1,1,color},{1,1,z,0,0,1,1,0,color},{-1,-1,z,0,0,1,0,1,color},{1,1,z,0,0,1,1,0,color},{-1,1,z,0,0,1,0,0,color}}};}
RenderPlacement placement(float z=-4){RenderPlacement p{};p.origin={0,0,z};p.worldUnitsPerBlock=1;p.drawAvatar=false;p.drawScene=true;p.drawDynamic=true;return p;}
void texture(Renderer& r,uint32_t id,uint8_t red,uint8_t green,uint8_t blue,uint8_t alpha){uint8_t rgba[16]{};for(int n=0;n<4;++n){rgba[n*4]=red;rgba[n*4+1]=green;rgba[n*4+2]=blue;rgba[n*4+3]=alpha;}check(r.uploadTexture(id,2,2,rgba,sizeof(rgba)),"effect texture rejected");}
uint32_t pixel(IDirect3DDevice9* d){IDirect3DSurface9 *rt{},*copy{};hr(d->GetRenderTarget(0,&rt),"read target");hr(d->CreateOffscreenPlainSurface(Size,Size,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr),"readback surface");HRESULT result=d->GetRenderTargetData(rt,copy);rt->Release();hr(result,"readback pixels");D3DLOCKED_RECT locked{};hr(copy->LockRect(&locked,nullptr,D3DLOCK_READONLY),"readback lock");uint32_t color{};std::memcpy(&color,static_cast<const uint8_t*>(locked.pBits)+(Size/2)*locked.Pitch+(Size/2)*4,4);copy->UnlockRect();copy->Release();return color;}
void begin(IDirect3DDevice9* d,float depth=1,uint32_t color=0xff0000ff){hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,color,depth,0),"effect clear");hr(d->BeginScene(),"effect begin");}
void submit(Renderer& r,IDirect3DDevice9* d,float z=-4){bool valid{};check(r.render(d,camera(),placement(z),&valid)&&valid,"effect mesh not submitted");}
uint32_t finish(IDirect3DDevice9* d){hr(d->EndScene(),"effect end");return pixel(d);}
int red(uint32_t c){return (c>>16)&255;}int green(uint32_t c){return (c>>8)&255;}int blue(uint32_t c){return c&255;}
void expectChannel(int actual,int expected,const char* text){check(std::abs(actual-expected)<=2,text);}
}
int main(){IDirect3D9* api{};IDirect3DDevice9* device{};HWND window{};try{
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="RE4CraftEffectsFixture";check(RegisterClassA(&wc)!=0,"register hidden fixture");window=CreateWindowExA(0,wc.lpszClassName,"RE4Craft effects test",WS_OVERLAPPEDWINDOW,0,0,Size,Size,nullptr,nullptr,wc.hInstance,nullptr);check(window!=nullptr,"hidden fixture window");
    api=Direct3DCreate9(D3D_SDK_VERSION);check(api!=nullptr,"D3D9 factory");D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;p.BackBufferWidth=p.BackBufferHeight=Size;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.EnableAutoDepthStencil=TRUE;p.AutoDepthStencilFormat=D3DFMT_D24S8;hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device),"D3D9 effects device");
    {
        Renderer effect,backgroundRenderer;
        texture(effect,1,255,0,0,64);check(effect.setMesh(3,{quad(1,8)}),"opaque particle rejected");
        begin(device);submit(effect,device);auto color=finish(device);expectChannel(red(color),255,"particle 25% alpha discarded by old50% cutoff");expectChannel(blue(color),0,"opaque particle unexpectedly blended");
        check(effect.setMesh(3,{quad(1,0)}),"ordinary cutout rejected");begin(device);submit(effect,device);color=finish(device);expectChannel(red(color),0,"ordinary opaque cutoff changed");expectChannel(blue(color),255,"ordinary opaque failed alpha cutoff");
        texture(effect,1,255,0,0,25);check(effect.setMesh(3,{quad(1,10)}),"translucent particle rejected");begin(device);submit(effect,device);color=finish(device);expectChannel(red(color),0,"particle accepted alpha belowvanilla0.1");
        texture(effect,1,255,0,0,26);begin(device);submit(effect,device);color=finish(device);expectChannel(red(color),26,"particle lost alpha immediately abovevanilla0.1");expectChannel(blue(color),229,"particle alpha blend incorrect");
        texture(effect,1,255,0,0,64);check(backgroundRenderer.setMesh(3,{quad(0,0,0xff00ff00)}),"far surface rejected");
        begin(device);submit(effect,device);submit(backgroundRenderer,device,-5);color=finish(device);expectChannel(red(color),64,"actual particle default depthwrite missing");expectChannel(green(color),0,"far opaque surface crossed particle depth");
        check(effect.setMesh(3,{quad(1,2)}),"ordinary translucent rejected");begin(device);submit(effect,device);submit(backgroundRenderer,device,-5);color=finish(device);expectChannel(green(color),255,"ordinary transparent material incorrectly wrote depth");
        texture(effect,2,64,64,64,255);
        // Capture order is intentionally reversed: cracks are still drawn
        // after all opaque faces and include the vanilla forward polygon bias.
        check(effect.setMesh(3,{quad(2,4,0xffffffff,-.000003f),quad(0,0,0xff80c040)}),"coplanar crumbling rejected");
        DWORD sentinelBias=0x3a83126f,sentinelSlope=0x3f000000;hr(device->SetRenderState(D3DRS_DEPTHBIAS,sentinelBias),"sentinel depth bias");hr(device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,sentinelSlope),"sentinel slope");hr(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"sentinel source blend");hr(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"sentinel destination blend");hr(device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE),"sentinel separate alpha");
        begin(device);submit(effect,device);color=finish(device);expectChannel(red(color),64,"crumbling did not multiply source and destination");expectChannel(green(color),96,"crumbling overlay hidden bycoplanardepth or wrong blend");expectChannel(blue(color),32,"crumbling texture color changed");
        for(auto item:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_DEPTHBIAS,sentinelBias},{D3DRS_SLOPESCALEDEPTHBIAS,sentinelSlope},{D3DRS_SRCBLEND,D3DBLEND_ONE},{D3DRS_DESTBLEND,D3DBLEND_ZERO},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE}}){DWORD actual{};hr(device->GetRenderState(item.first,&actual),"restored material state");check(actual==item.second,"effect material leaked native state");}
        check(effect.setMesh(3,{quad(2,4)}),"crumbling pass rejected");begin(device,.1f);submit(effect,device);color=finish(device);expectChannel(blue(color),255,"crumbling bias ignored native wall depth");expectChannel(red(color),0,"crumbling appeared through native wall");
        begin(device,1,0xff80c040);submit(effect,device);submit(backgroundRenderer,device,-5);color=finish(device);expectChannel(green(color),255,"crumbling wrote depth and occluded later surface");
        uint8_t repeatPixels[]={64,64,64,255,192,192,192,255,32,32,32,255,128,128,128,255};
        check(effect.uploadTexture(4,2,2,repeatPixels,sizeof(repeatPixels)),"repeat fixture texture rejected");
        auto outside=quad(4,4);for(auto& v:outside.vertices){v.u=v.v=-.25f;}
        check(effect.setMesh(3,{outside}),"negativeUV crumbling rejected");
        hr(device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_MIRROR),"sentinel addressU");hr(device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_BORDER),"sentinel addressV");
        begin(device,1,0xff808080);submit(effect,device);color=finish(device);expectChannel(red(color),129,"negative crumblingUV clamped instead of vanilla repeat");
        DWORD restoredAddress{};hr(device->GetSamplerState(0,D3DSAMP_ADDRESSU,&restoredAddress),"restored addressU");check(restoredAddress==D3DTADDRESS_MIRROR,"crumbling leaked repeatU sampler");hr(device->GetSamplerState(0,D3DSAMP_ADDRESSV,&restoredAddress),"restored addressV");check(restoredAddress==D3DTADDRESS_BORDER,"crumbling leaked repeatV sampler");
        for(auto& v:outside.vertices){v.u=v.v=1.25f;}check(effect.setMesh(3,{outside}),"positiveUV crumbling rejected");begin(device,1,0xff808080);submit(effect,device);color=finish(device);expectChannel(red(color),64,"positive crumblingUV clamped instead of vanilla repeat");
        auto ordinary=outside;ordinary.flags=2;for(auto& v:ordinary.vertices)v.z=.5f;
        check(effect.setMesh(3,{outside,ordinary}),"mixed sampler materials rejected");begin(device,1,0xff808080);submit(effect,device);color=finish(device);expectChannel(red(color),128,"crumbling repeat leaked toordinary atlas material");
        texture(effect,3,40,80,120,255);check(effect.setMesh(3,{quad(3,16)}),"vanilla TNT white overlay rejected");
        DWORD textureFactor=0x12345678;hr(device->SetRenderState(D3DRS_TEXTUREFACTOR,textureFactor),"sentinel overlay factor");
        hr(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE),"sentinel overlay stage");
        begin(device);submit(effect,device);color=finish(device);expectChannel(red(color),202,"TNT white overlay wrong base weight");expectChannel(green(color),212,"TNT white overlay wrong green");expectChannel(blue(color),222,"TNT white overlay wrong blue");
        DWORD restored{};hr(device->GetRenderState(D3DRS_TEXTUREFACTOR,&restored),"restored overlay factor");check(restored==textureFactor,"TNT overlay leaked texture factor");hr(device->GetTextureStageState(1,D3DTSS_COLOROP,&restored),"restored overlay stage");check(restored==D3DTOP_DISABLE,"TNT overlay leaked texture stage");
        texture(effect,3,40,80,120,64);check(effect.setMesh(3,{quad(3,18)}),"overlay withalpha blend rejected");begin(device);submit(effect,device);color=finish(device);expectChannel(red(color),51,"TNT overlay altered source alpha");expectChannel(green(color),53,"TNT overlay alpha modulation incorrect");expectChannel(blue(color),247,"TNT overlay changed destination blend");
        texture(effect,3,40,80,120,255);check(effect.setMesh(3,{quad(3,16),quad(0,0,0xff00ff00,.5f)}),"mixed overlay andordinary mesh rejected");begin(device);submit(effect,device);color=finish(device);expectChannel(red(color),0,"white overlay leaked toordinary following batch");expectChannel(green(color),255,"ordinary mesh color inherited TNT overlay");
        auto invalid=quad(0,6);check(!effect.setMesh(3,{invalid}),"ambiguous crumbling+alpha accepted");invalid.flags=12;check(!effect.setMesh(3,{invalid}),"ambiguous crumbling+particle accepted");invalid.flags=20;check(!effect.setMesh(3,{invalid}),"ambiguous crumbling+whiteoverlay accepted");invalid.flags=32;check(!effect.setMesh(3,{invalid}),"unknown material flag accepted");
    }
    device->Release();device=nullptr;api->Release();api=nullptr;DestroyWindow(window);window=nullptr;std::puts("PASS: native particle cutoff/blend/depth, crumbling multiply/forward-bias/host-occlusion, vanilla TNT white overlay, material validation and state restoration.");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Effects fixture failed: %s\n",e.what());if(device)device->Release();if(api)api->Release();if(window)DestroyWindow(window);return 1;}}
