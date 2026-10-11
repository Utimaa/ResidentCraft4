#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace {
void hr(HRESULT result,const char* name){if(FAILED(result)){char text[256];std::snprintf(text,sizeof(text),"%s (HRESULT0x%08lx)",name,static_cast<unsigned long>(result));throw std::runtime_error(text);}}
void check(bool value,const char* name){if(!value)throw std::runtime_error(name);}
constexpr unsigned Iterations=10000;
struct Transforms {D3DMATRIX world{},view{},projection{};D3DVIEWPORT9 viewport{};};
Transforms read(IDirect3DDevice9* d){Transforms t;hr(d->GetTransform(D3DTS_WORLD,&t.world),"world");hr(d->GetTransform(D3DTS_VIEW,&t.view),"view");hr(d->GetTransform(D3DTS_PROJECTION,&t.projection),"projection");hr(d->GetViewport(&t.viewport),"viewport");return t;}
void restore(IDirect3DDevice9* d,const Transforms& t){hr(d->SetTransform(D3DTS_WORLD,&t.world),"restore world");hr(d->SetTransform(D3DTS_VIEW,&t.view),"restore view");hr(d->SetTransform(D3DTS_PROJECTION,&t.projection),"restore projection");hr(d->SetViewport(&t.viewport),"restore viewport");}
void baseline(IDirect3DDevice9* d,DWORD alpha){hr(d->SetRenderState(D3DRS_ALPHABLENDENABLE,alpha),"alpha");hr(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS),"zfunc");hr(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP),"sampler");hr(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"FVF");}
void changed(IDirect3DDevice9* d){hr(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"changed alpha");hr(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS),"changed zfunc");hr(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"changed sampler");}
void verify(IDirect3DDevice9* d,DWORD alpha){DWORD value{};hr(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&value),"get alpha");check(value==alpha,"alpha state not restored");hr(d->GetRenderState(D3DRS_ZFUNC,&value),"get zfunc");check(value==D3DCMP_LESS,"depth state not restored");hr(d->GetSamplerState(0,D3DSAMP_ADDRESSU,&value),"get sampler");check(value==D3DTADDRESS_WRAP,"sampler not restored");}
double bench(IDirect3DDevice9* d,bool reuse){
    IDirect3DStateBlock9* shared{};if(reuse)hr(d->CreateStateBlock(D3DSBT_ALL,&shared),"shared state block");
    auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<Iterations;i++){
        auto transform=read(d);IDirect3DStateBlock9* block=shared;
        if(!reuse)hr(d->CreateStateBlock(D3DSBT_ALL,&block),"state block");
        hr(block->Capture(),"capture");changed(d);hr(block->Apply(),"apply");restore(d,transform);
        if(!reuse)block->Release();
    }
    double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count()/Iterations;
    if(shared)shared->Release();return us;
}
}
int main(){
    HWND window{};IDirect3D9* api{};IDirect3DDevice9* device{};
    try{
        WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="RE4CraftStateBenchmark";RegisterClassA(&wc);
        window=CreateWindowExA(0,wc.lpszClassName,"RE4Craft state benchmark",WS_OVERLAPPEDWINDOW,0,0,160,120,nullptr,nullptr,wc.hInstance,nullptr);check(window!=nullptr,"window");
        api=Direct3DCreate9(D3D_SDK_VERSION);check(api!=nullptr,"Direct3DCreate9");
        D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;p.BackBufferWidth=160;p.BackBufferHeight=120;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.EnableAutoDepthStencil=TRUE;p.AutoDepthStencilFormat=D3DFMT_D24S8;
        hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device),"HAL device");
        baseline(device,FALSE);bench(device,false);verify(device,FALSE);bench(device,true);verify(device,FALSE);
        double created=0,reused=0;
        for(int sample=0;sample<3;sample++){created+=bench(device,false);reused+=bench(device,true);}
        verify(device,FALSE);
        // A reusable block must be released before Reset. After recreation,
        // capture must acquire changed host state, not retain the old snapshot.
        hr(device->Reset(&p),"Reset after released state blocks");baseline(device,TRUE);
        bench(device,true);verify(device,TRUE);
        std::ofstream out("../build/state-block-benchmark/report.json");
        out<<"{\"iterations_per_sample\":"<<Iterations<<",\"samples\":3,\"create_capture_apply_release_us\":"<<created/3<<",\"reuse_capture_apply_us\":"<<reused/3<<",\"state_restore_and_reset_passed\":true,\"scope\":\"Standalone HAL device; includes production-style explicit transform/viewport save+restore. This is not live RE4 FPS.\"}";
        std::printf("StateBlock benchmark PASS: create/capture/apply/release %.3fus, reused capture/apply %.3fus; host state and Reset restore verified.\n",created/3,reused/3);
        device->Release();api->Release();DestroyWindow(window);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"StateBlock benchmark FAIL: %s\n",error.what());if(device)device->Release();if(api)api->Release();if(window)DestroyWindow(window);return 1;}
}
