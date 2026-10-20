#pragma once
#include <windows.h>
#include <d3d9.h>
#include <atomic>
#include <array>
#include "transport.hpp"
#include "camera_control.hpp"
namespace re4craft {
class HostInput {
public:
    bool attach(IDirect3DDevice9* device,Transport* transport,CameraControlBridge* camera);
    void setActive(bool active,bool screenOpen);
    void sample(Input& input);
    bool consumeKeyPress(int key){return key>0&&key<256&&keyPresses_[key].exchange(false);}
    LRESULT message(HWND,UINT,WPARAM,LPARAM);
    void wheel(int delta);
private:
    WNDPROC original_{};
    HWND window_{};
    std::atomic<Transport*> transport_{};
    CameraControlBridge* camera_{};
    std::atomic<bool> active_{},screen_{};
    std::atomic<ULONGLONG> heartbeat_{};
    std::atomic<int> scroll_{};
    std::atomic<bool> wheelHooked_{};
    std::atomic<float> cursorX_{.5f},cursorY_{.5f};
    std::atomic<ULONGLONG> rawTime_{};
    POINT previousCursor_{};
    bool haveCursor_{};
    void mouseDelta(float dx,float dy);
    uint16_t highSurrogate_{};
    std::array<std::atomic<bool>,256> keyPresses_{};
};
}
