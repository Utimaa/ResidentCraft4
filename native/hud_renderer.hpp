#pragma once
#include <d3d9.h>
#include "transport.hpp"
namespace re4craft {
class HudRenderer {
public:
    ~HudRenderer(){onDeviceLost();}
    void update(HudData data){data_=std::move(data);dirty_=true;}
    void clear(){onDeviceLost();data_={};dirty_=false;}
    void onDeviceLost();
    bool render(IDirect3DDevice9* device,bool cursor=false,float cursorX=0,float cursorY=0);
private:
    HudData data_;
    IDirect3DTexture9* texture_{};
    IDirect3DDevice9* device_{};
    bool dirty_{};
    bool textureDynamic_{};
    uint32_t textureWidth_{},textureHeight_{};
};
}
