#include "hud_renderer.hpp"
#include <algorithm>
#include <cstring>
namespace re4craft {
void HudRenderer::onDeviceLost(){if(texture_)texture_->Release();texture_=nullptr;textureWidth_=textureHeight_=0;textureDynamic_=false;device_=nullptr;dirty_=true;}
bool HudRenderer::render(IDirect3DDevice9* d,bool cursor,float cursorX,float cursorY){
    if(!d||!data_.width||!data_.height||data_.width>1920||data_.height>1080||(data_.flags&~1u)||data_.rgba.size()!=size_t(data_.width)*data_.height*4)return false;
    if(device_!=d){onDeviceLost();device_=d;}
    if(dirty_){
        if(texture_&&(textureWidth_!=data_.width||textureHeight_!=data_.height)){texture_->Release();texture_=nullptr;}
        if(!texture_){
            // Frequent full-frame updates can discard old storage while the GPU
            // still samples it. Older devices retain the managed fallback.
            textureDynamic_=SUCCEEDED(d->CreateTexture(data_.width,data_.height,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture_,nullptr));
            if(!textureDynamic_&&FAILED(d->CreateTexture(data_.width,data_.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture_,nullptr)))return false;
            textureWidth_=data_.width;textureHeight_=data_.height;
        }
        D3DLOCKED_RECT locked{};if(FAILED(texture_->LockRect(0,&locked,nullptr,textureDynamic_?D3DLOCK_DISCARD:0))){onDeviceLost();return false;}
        for(uint32_t y=0;y<data_.height;y++){
            auto* dst=static_cast<uint8_t*>(locked.pBits)+size_t(y)*locked.Pitch;
            uint32_t sourceY=(data_.flags&1)?data_.height-y-1:y;
            const auto* src=data_.rgba.data()+size_t(sourceY)*data_.width*4;
            for(uint32_t x=0;x<data_.width;x++){uint32_t pixel;std::memcpy(&pixel,src+x*4,4);pixel=(pixel&0xff00ff00u)|((pixel&0xffu)<<16)|((pixel&0xff0000u)>>16);std::memcpy(dst+x*4,&pixel,4);}
        }
        texture_->UnlockRect(0);dirty_=false;
    }
    IDirect3DStateBlock9* state{};
    if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&state))||!state)return false;
    if(FAILED(state->Capture())){state->Release();return false;}
    D3DVIEWPORT9 vp{};d->GetViewport(&vp);
    struct ScreenVertex{float x,y,z,rhw;DWORD color;float u,v;};
    const float x=float(vp.X)-.5f,y=float(vp.Y)-.5f,w=float(vp.Width),h=float(vp.Height);
    ScreenVertex quad[]{{x,y,0,1,0xffffffff,0,0},{x+w,y,0,1,0xffffffff,1,0},{x,y+h,0,1,0xffffffff,0,1},{x+w,y+h,0,1,0xffffffff,1,1}};
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    // Vanilla's transparent framebuffer stores premultiplied colour.
    d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    d->SetTexture(0,texture_);d->SetTexture(1,nullptr);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    bool ok=SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(ScreenVertex)));
    if(cursor){
        d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2);
        float cx=x+std::clamp(cursorX,0.f,1.f)*w,cy=y+std::clamp(cursorY,0.f,1.f)*h;
        ScreenVertex pointer[]{{cx,cy,0,1,0xff000000,0,0},{cx+12,cy+18,0,1,0xff000000,0,0},{cx,cy+21,0,1,0xff000000,0,0},
            {cx+2,cy+3,0,1,0xffffffff,0,0},{cx+9,cy+16,0,1,0xffffffff,0,0},{cx+2,cy+17,0,1,0xffffffff,0,0}};
        d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,pointer,sizeof(ScreenVertex));
    }
    state->Apply();state->Release();return ok;
}
}
