#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include "../native/renderer.hpp"
#include "../protocol/re4craft_protocol.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace re4craft;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void hr(HRESULT value,const char* message){check(SUCCEEDED(value),message);}
NativeSnapshot camera(){NativeSnapshot s{};s.valid=s.alive=s.playing=true;s.view[0][0]=s.view[1][1]=s.view[2][2]=1;s.projection[0][0]=s.projection[1][1]=2.41421356f;s.projection[2][2]=-1.f/99;s.projection[2][3]=-100.f/99;s.projection[3][2]=-1;return s;}
RenderPlacement placement(){RenderPlacement p{};p.origin={0,-.8f,-4};p.worldUnitsPerBlock=1;return p;}
std::vector<RenderBatch> load(Renderer& renderer,const char* path){
    std::ifstream input(path,std::ios::binary);check(bool(input),"open actual Minecraft capture");
    std::map<uint32_t,bool> binaryAlpha;std::vector<RenderBatch> result;
    while(true){Header h{};input.read(reinterpret_cast<char*>(&h),sizeof(h));if(input.eof()&&input.gcount()==0)break;
        check(input.gcount()==sizeof(h)&&h.magic==Magic&&(h.version==4||h.version==ProtocolVersion)&&h.size<=MaxPayload,"capture header");
        std::vector<uint8_t> bytes(h.size);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());check(bool(input),"capture payload");
        if(h.type==Message::Texture){check(bytes.size()>=sizeof(Texture),"texture prefix");Texture t{};std::memcpy(&t,bytes.data(),sizeof(t));
            check(renderer.uploadTexture(t.id,t.width,t.height,bytes.data()+sizeof(t),bytes.size()-sizeof(t)),"upload real texture");
            bool binary=true;for(size_t n=sizeof(t)+3;n<bytes.size();n+=4)binary&=bytes[n]==0||bytes[n]==255;binaryAlpha[t.id]=binary;
        } else if(h.type==Message::Mesh){Mesh mesh{};check(bytes.size()>=sizeof(mesh),"mesh prefix");std::memcpy(&mesh,bytes.data(),sizeof(mesh));if(mesh.category!=1)continue;
            size_t offset=sizeof(mesh);result.clear();for(uint32_t n=0;n<mesh.batchCount;++n){Batch b{};check(offset+sizeof(b)<=bytes.size(),"batch prefix");std::memcpy(&b,bytes.data()+offset,sizeof(b));offset+=sizeof(b);
                check(b.vertexCount<=MaxVertices&&size_t(b.vertexCount)*sizeof(Vertex)<=bytes.size()-offset,"batch vertices");RenderBatch out{b.textureId,b.flags,{}};out.vertices.resize(b.vertexCount);
                std::memcpy(out.vertices.data(),bytes.data()+offset,size_t(b.vertexCount)*sizeof(Vertex));offset+=size_t(b.vertexCount)*sizeof(Vertex);
                // Historical skin captures predate source cutout promotion.
                // Qualify their complete source texture and every vertex first.
                if(out.flags==2&&binaryAlpha[b.textureId]&&std::all_of(out.vertices.begin(),out.vertices.end(),[](auto& v){return (v.color>>24)==255;}))out.flags=0;
                check((out.flags&2)==0,"stream fixture requires genuine binary cutout/opaque material");result.push_back(std::move(out));
            }check(offset==bytes.size(),"mesh suffix");
        }
    }check(!result.empty(),"real avatar missing");return result;
}
double center(IDirect3DDevice9* d){IDirect3DSurface9 *target{},*copy{};hr(d->GetRenderTarget(0,&target),"read target");hr(d->CreateOffscreenPlainSurface(256,256,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr),"readback surface");HRESULT result=d->GetRenderTargetData(target,copy);target->Release();hr(result,"readback");D3DLOCKED_RECT lock{};hr(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY),"readback lock");double sum=0;size_t count=0;for(int y=0;y<256;++y){auto row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(lock.pBits)+y*lock.Pitch);for(int x=0;x<256;++x)if((row[x]&0xffffff)!=0){sum+=x;++count;}}copy->UnlockRect();copy->Release();check(count>100,"actual skin pixels missing");return sum/count;}
void render(Renderer& renderer,IDirect3DDevice9* d,const std::vector<RenderBatch>& source,float shift,bool present=true){auto moved=source;for(auto& b:moved)for(auto& v:b.vertices)v.x+=shift;check(renderer.setMesh(1,std::move(moved)),"changing real mesh rejected");hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0),"clear");hr(d->BeginScene(),"begin");check(renderer.render(d,camera(),placement()),"actual mesh render");hr(d->EndScene(),"end");if(present)hr(d->Present(nullptr,nullptr,nullptr,nullptr),"present");}
}
int main(int argc,char** argv){IDirect3D9* api{};IDirect3DDevice9* device{};HWND window{};try{
    check(argc>=2,"usage: stream_harness actual-avatar.bin");WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="RE4CraftStreamFixture";check(RegisterClassA(&wc)!=0,"window class");window=CreateWindowExA(0,wc.lpszClassName,"RE4Craft stream fixture",WS_OVERLAPPEDWINDOW,0,0,256,256,nullptr,nullptr,wc.hInstance,nullptr);check(window!=nullptr,"hidden window");
    api=Direct3DCreate9(D3D_SDK_VERSION);check(api!=nullptr,"D3D9 factory");D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;p.BackBufferWidth=p.BackBufferHeight=256;p.BackBufferFormat=D3DFMT_X8R8G8B8;p.EnableAutoDepthStencil=TRUE;p.AutoDepthStencilFormat=D3DFMT_D24S8;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device),"D3D9 HAL device");
    {Renderer renderer;auto source=load(renderer,argv[1]);
        render(renderer,device,source,-.6f,false);double left=center(device);render(renderer,device,source,.6f,false);double right=center(device);check(right-left>35,"changing vertices reused stale GPU contents");
        for(int n=0;n<12;++n){render(renderer,device,source,n%2?.6f:-.6f,false);double x=center(device);check(std::abs(x-(n%2?right:left))<1,"alternating changed vertices corrupted");}
        renderer.onDeviceLost();hr(device->Reset(&p),"release streaming DEFAULT buffers before Reset");render(renderer,device,source,-.6f,false);check(std::abs(center(device)-left)<1,"stream mesh failed GPU recreation after actual Reset");
        // Bounded dense uploads reuse only actual Minecraft vertices/UVs. This
        // isolates buffer streaming cost; it does not measure overall RE4 FPS.
        auto dense=source;size_t vertices=0;for(auto& batch:dense){auto original=batch.vertices;batch.vertices.clear();for(int copy=0;copy<64;++copy)batch.vertices.insert(batch.vertices.end(),original.begin(),original.end());vertices+=batch.vertices.size();}
        std::vector<double> times;for(int n=0;n<260;++n){auto start=std::chrono::steady_clock::now();render(renderer,device,dense,n%2?.015f:-.015f);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(n>=20)times.push_back(ms);}
        std::sort(times.begin(),times.end());std::printf("PASS actual cutout stream: changing vertices, 12 readbacks, actual device Reset; dense=%zu vertices median=%.3fms p95=%.3fms max=%.3fms\n",vertices,times[times.size()/2],times[times.size()*95/100],times.back());
    }device->Release();api->Release();DestroyWindow(window);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());if(device)device->Release();if(api)api->Release();if(window)DestroyWindow(window);return 1;}}
