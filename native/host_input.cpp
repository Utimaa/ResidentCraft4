#include "host_input.hpp"
#include <algorithm>
#include <cstring>
#include "input_mapping.hpp"
namespace re4craft { namespace {
HostInput* current{};
LRESULT CALLBACK bridgeProcedure(HWND h,UINT m,WPARAM w,LPARAM l){return current?current->message(h,m,w,l):DefWindowProcW(h,m,w,l);}
// WM_MOUSEWHEEL reaches the focus window through every subclass installed by
// RE4 and other mods (re4_tweaks' ImGui), and was intermittently consumed
// before ours. A low-level mouse hook sees the wheel before any window does.
std::atomic<bool> wheelHookReady{};
LRESULT CALLBACK wheelProcedure(int code,WPARAM w,LPARAM l){
    if(code==HC_ACTION&&w==WM_MOUSEWHEEL&&current)current->wheel(GET_WHEEL_DELTA_WPARAM(reinterpret_cast<MSLLHOOKSTRUCT*>(l)->mouseData));
    return CallNextHookEx(nullptr,code,w,l);
}
DWORD WINAPI wheelThread(void*){
    HMODULE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&wheelProcedure),&module);
    HHOOK hook=SetWindowsHookExW(WH_MOUSE_LL,wheelProcedure,module,0);if(!hook)return 0;
    wheelHookReady=true;MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){}
    wheelHookReady=false;UnhookWindowsHookEx(hook);return 0;
}
uint32_t scancode(WPARAM vk,LPARAM flags){
    uint32_t make=uint32_t((flags>>16)&255);if(!make)make=MapVirtualKeyExW(UINT(vk),MAPVK_VK_TO_VSC_EX,GetKeyboardLayout(0))&255;
    return sdlPhysicalScancode(make,(flags&(1LL<<24))!=0);
}
}
void HostInput::setActive(bool active,bool screenOpen){
    heartbeat_=GetTickCount64();if(active!=active_.load()||screenOpen!=screen_.load()){
        haveCursor_=GetCursorPos(&previousCursor_)!=FALSE;highSurrogate_=0;
        if(!active)scroll_=0;
        // RE4 may re-register its mouse device when returning from a menu.
        if(active&&window_){RAWINPUTDEVICE raw{1,2,RIDEV_INPUTSINK,window_};RegisterRawInputDevices(&raw,1,sizeof(raw));}
    }
    active_=active;screen_=screenOpen;
}
void HostInput::mouseDelta(float dx,float dy){
    if(screen_){RECT rect{};GetClientRect(window_,&rect);cursorX_=moveCursor(cursorX_.load(),dx,std::max<LONG>(1,rect.right));cursorY_=moveCursor(cursorY_.load(),dy,std::max<LONG>(1,rect.bottom));}
    else if(camera_)camera_->addMouseDelta(dx,dy);
}
namespace {
uint32_t modifiers(){uint32_t mods=0;const int keys[]{VK_LSHIFT,VK_RSHIFT,VK_LCONTROL,VK_RCONTROL,VK_LMENU,VK_RMENU};const uint32_t bits[]{1,2,0x40,0x80,0x100,0x200};for(int i=0;i<6;i++)if(GetAsyncKeyState(keys[i])&0x8000)mods|=bits[i];if(GetKeyState(VK_CAPITAL)&1)mods|=0x2000;if(GetKeyState(VK_NUMLOCK)&1)mods|=0x1000;return mods;}
}
bool HostInput::attach(IDirect3DDevice9* device,Transport* transport,CameraControlBridge* camera){
    transport_.store(transport,std::memory_order_release);camera_=camera;
    if(window_)return true;
    D3DDEVICE_CREATION_PARAMETERS params{};if(!device||FAILED(device->GetCreationParameters(&params))||!params.hFocusWindow)return false;
    window_=params.hFocusWindow;current=this;
    SetLastError(0);auto old=SetWindowLongPtrW(window_,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&bridgeProcedure));
    if(!old&&GetLastError()){window_=nullptr;current=nullptr;return false;}original_=reinterpret_cast<WNDPROC>(old);
    RAWINPUTDEVICE raw{1,2,RIDEV_INPUTSINK,window_};RegisterRawInputDevices(&raw,1,sizeof(raw));
    if(HANDLE thread=CreateThread(nullptr,0,wheelThread,nullptr,0,nullptr))CloseHandle(thread);
    return true;
}
void HostInput::wheel(int delta){
    DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    if(active_&&GetTickCount64()-heartbeat_.load()<=750&&foregroundPid==GetCurrentProcessId())scroll_.fetch_add(delta);
}
void HostInput::sample(Input& input){
    RECT rect{};if(window_)GetClientRect(window_,&rect);input.viewportWidth=std::clamp<LONG>(rect.right,1,16384);input.viewportHeight=std::clamp<LONG>(rect.bottom,1,16384);
    POINT cursor{};if(GetCursorPos(&cursor)){
        if(active_&&screen_&&haveCursor_&&GetTickCount64()-rawTime_.load()>50){
            // Relative raw input is preferred; this supports ordinary windowed
            // cursors when the game has temporarily replaced its raw registration.
            auto nativeDelta=camera_?camera_->readMouseDelta():InputMouseDelta{};
            if(nativeDelta.x!=0||nativeDelta.y!=0)mouseDelta(nativeDelta.x,nativeDelta.y);
            else mouseDelta(float(cursor.x-previousCursor_.x),float(cursor.y-previousCursor_.y));
        }
        previousCursor_=cursor;haveCursor_=true;
    }
    input.cursorX=cursorX_.load();input.cursorY=cursorY_.load();input.scroll=scroll_.exchange(0);
    if(GetAsyncKeyState(VK_LBUTTON)&0x8000)input.mouseButtons|=1;if(GetAsyncKeyState(VK_RBUTTON)&0x8000)input.mouseButtons|=2;if(GetAsyncKeyState(VK_MBUTTON)&0x8000)input.mouseButtons|=4;
}
LRESULT HostInput::message(HWND h,UINT m,WPARAM w,LPARAM l){
    DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    if(foregroundPid==GetCurrentProcessId()&&(m==WM_KEYDOWN||m==WM_SYSKEYDOWN)&&w>0&&w<256&&!(l&(1LL<<30)))keyPresses_[w].store(true);
    if(active_&&GetTickCount64()-heartbeat_.load()<=750&&foregroundPid==GetCurrentProcessId()){
        if(m==WM_INPUT){
            RAWINPUT raw{};UINT size=sizeof(raw);
            if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))!=UINT(-1)&&raw.header.dwType==RIM_TYPEMOUSE&&!(raw.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){
                const float dx=float(raw.data.mouse.lLastX),dy=float(raw.data.mouse.lLastY);
                rawTime_=GetTickCount64();mouseDelta(dx,dy);
            }
            return DefWindowProcW(h,m,w,l);
        }
        if(m==WM_MOUSEWHEEL){if(!wheelHookReady)scroll_.fetch_add(GET_WHEEL_DELTA_WPARAM(w));return 0;}
        auto* peer=transport_.load(std::memory_order_acquire);
        if(screen_&&peer){
            if(m==WM_KEYDOWN||m==WM_KEYUP||m==WM_SYSKEYDOWN||m==WM_SYSKEYUP){
                if(w==VK_F4&&(GetAsyncKeyState(VK_MENU)&0x8000))return CallWindowProcW(original_,h,m,w,l);
                uint32_t scan=scancode(w,l);if(scan){KeyEvent key{scan,(m==WM_KEYUP||m==WM_SYSKEYUP)?0:((l&(1LL<<30))?-1:1),modifiers(),0};
                    std::vector<uint8_t> payload(sizeof(key));std::memcpy(payload.data(),&key,sizeof(key));peer->sendPacket(Message::KeyEvent,std::move(payload));}
                return 0;
            }
            if(m==WM_CHAR){
                wchar_t chars[2]{wchar_t(w),0};int count=1;
                if(w>=0xd800&&w<=0xdbff){highSurrogate_=uint16_t(w);return 0;}
                if(w>=0xdc00&&w<=0xdfff&&highSurrogate_){chars[0]=wchar_t(highSurrogate_);chars[1]=wchar_t(w);count=2;}highSurrogate_=0;
                if(w>=32){char utf8[8]{};int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,chars,count,utf8,sizeof(utf8),nullptr,nullptr);if(bytes>0)peer->sendPacket(Message::Text,{utf8,utf8+bytes});}
                return 0;
            }
        }
    }
    return CallWindowProcW(original_,h,m,w,l);
}
}
