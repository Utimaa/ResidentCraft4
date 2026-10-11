#pragma once
#include <cstdint>
#include <algorithm>
namespace re4craft {
// Windows Set-1 make codes and SDL USB usages describe physical keys, independent
// of the user's layout. Text is sent separately as UTF-8 from WM_CHAR.
// Checked against SDL's src/events/scancodes_windows.h and Microsoft MapVirtualKeyEx.
constexpr uint32_t sdlPhysicalScancode(uint32_t code,bool extended=false){
    if(extended){switch(code){
        case 0x1c:return 88;case 0x1d:return 228;case 0x35:return 84;case 0x36:return 229;case 0x37:return 70;case 0x38:return 230;
        case 0x47:return 74;case 0x48:return 82;case 0x49:return 75;case 0x4b:return 80;case 0x4d:return 79;case 0x4f:return 77;
        case 0x50:return 81;case 0x51:return 78;case 0x52:return 73;case 0x53:return 76;case 0x5b:return 227;case 0x5c:return 231;case 0x5d:return 101;default:return 0;}}
    if(code>=0x02&&code<=0x0a)return 30+code-2;
    if(code>=0x3b&&code<=0x44)return 58+code-0x3b;
    constexpr uint32_t upper[]{20,26,8,21,23,28,24,12,18,19};
    constexpr uint32_t middle[]{4,22,7,9,10,11,13,14,15};
    constexpr uint32_t lower[]{29,27,6,25,5,17,16};
    if(code>=0x10&&code<=0x19)return upper[code-0x10];
    if(code>=0x1e&&code<=0x26)return middle[code-0x1e];
    if(code>=0x2c&&code<=0x32)return lower[code-0x2c];
    switch(code){case 1:return 41;case 0x0b:return 39;case 0x0c:return 45;case 0x0d:return 46;case 0x0e:return 42;case 0x0f:return 43;
        case 0x1a:return 47;case 0x1b:return 48;case 0x1c:return 40;case 0x1d:return 224;case 0x27:return 51;case 0x28:return 52;
        case 0x29:return 53;case 0x2a:return 225;case 0x2b:return 49;case 0x33:return 54;case 0x34:return 55;case 0x35:return 56;
        case 0x36:return 229;case 0x37:return 85;case 0x38:return 226;case 0x39:return 44;case 0x3a:return 57;case 0x45:return 83;
        case 0x46:return 71;case 0x47:return 95;case 0x48:return 96;case 0x49:return 97;case 0x4a:return 86;case 0x4b:return 92;
        case 0x4c:return 93;case 0x4d:return 94;case 0x4e:return 87;case 0x4f:return 89;case 0x50:return 90;case 0x51:return 91;
        case 0x52:return 98;case 0x53:return 99;case 0x56:return 100;case 0x57:return 68;case 0x58:return 69;default:return 0;}
}
inline float moveCursor(float current,float delta,uint32_t dimension){return std::clamp(current+delta/float(std::max(1u,dimension)),0.f,1.f);}
}
