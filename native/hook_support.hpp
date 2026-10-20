#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <vector>
#include <cstdint>

namespace re4craft::hookdetail {
inline bool memory(uintptr_t address,size_t size,bool writable=false) {
    if (!address || size>UINTPTR_MAX-address) return false;
    for(uintptr_t end=address+size;address<end;) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(address),&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        DWORD p=m.Protect&0xff;
        if(writable && p!=PAGE_READWRITE && p!=PAGE_WRITECOPY && p!=PAGE_EXECUTE_READWRITE && p!=PAGE_EXECUTE_WRITECOPY)return false;
        uintptr_t next=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
        if(next<=address)return false;address=next<end?next:end;
    }
    return true;
}
inline bool copy(uintptr_t address,void* out,size_t size) {
    if(!memory(address,size))return false;
    __try {std::memcpy(out,reinterpret_cast<void*>(address),size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool write(uintptr_t address,const void* in,size_t size) {
    if(!memory(address,size,true))return false;
    __try {std::memcpy(reinterpret_cast<void*>(address),in,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline uintptr_t operand(uintptr_t address){uint32_t v{};return copy(address,&v,4)?v:0;}
struct ImageCode {
    const uint8_t* begin{};size_t size{};
    // Unpatched .text read from the executable on disk. Other loaded mods
    // (re4_tweaks) rewrite call sites and thunks in memory; signatures and
    // branch targets are resolved against this copy, while hooks still go on
    // the live, unmodified function bodies at the same addresses.
    std::vector<uint8_t> pristine;
    const uint8_t* scan()const{return pristine.empty()?begin:pristine.data();}
    bool initialize(){
        auto base=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER d{};IMAGE_NT_HEADERS32 n{};
        if(!copy(reinterpret_cast<uintptr_t>(base),&d,sizeof(d))||d.e_magic!=IMAGE_DOS_SIGNATURE||d.e_lfanew<=0)return false;
        if(!copy(reinterpret_cast<uintptr_t>(base)+d.e_lfanew,&n,sizeof(n))||n.Signature!=IMAGE_NT_SIGNATURE||n.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386)return false;
        uintptr_t sections=reinterpret_cast<uintptr_t>(base)+d.e_lfanew+24+n.FileHeader.SizeOfOptionalHeader;
        for(unsigned i=0;i<n.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER s{};if(!copy(sections+i*sizeof(s),&s,sizeof(s)))return false;if(!std::memcmp(s.Name,".text",5)){begin=base+s.VirtualAddress;size=s.Misc.VirtualSize;if(!memory(reinterpret_cast<uintptr_t>(begin),size))return false;loadPristine(s);return true;}}
        return false;
    }
    void loadPristine(const IMAGE_SECTION_HEADER& s){
        wchar_t path[MAX_PATH]{};if(!GetModuleFileNameW(nullptr,path,MAX_PATH))return;
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        std::vector<uint8_t> bytes(size,0);DWORD got{};size_t want=s.SizeOfRawData<size?s.SizeOfRawData:size;
        bool ok=SetFilePointer(file,static_cast<LONG>(s.PointerToRawData),nullptr,FILE_BEGIN)!=INVALID_SET_FILE_POINTER&&ReadFile(file,bytes.data(),static_cast<DWORD>(want),&got,nullptr)&&got==want;
        CloseHandle(file);
        // Reject an encrypted or mismatched file: most of the code must match memory.
        if(!ok)return;size_t same{};for(size_t i=0;i<want;i+=64)same+=bytes[i]==begin[i];
        if(same*10>=(want/64+1)*9)pristine=std::move(bytes);
    }
    bool contains(uintptr_t address,size_t count=1)const{return begin && address>=reinterpret_cast<uintptr_t>(begin) && count<=size && address-reinterpret_cast<uintptr_t>(begin)<=size-count;}
    uintptr_t find(const char* pattern)const{
        if(!pristine.empty()){if(auto found=findIn(pristine.data(),pattern))return found;}
        return findIn(begin,pattern);
    }
    uintptr_t findIn(const uint8_t* data,const char* pattern)const{
        std::vector<int> bytes;
        auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;};
        while(*pattern){if(*pattern==' '){++pattern;continue;}if(*pattern=='?'){bytes.push_back(-1);while(*pattern=='?')++pattern;continue;}int a=digit(pattern[0]),b=digit(pattern[1]);if(a<0||b<0)return 0;bytes.push_back(a*16+b);pattern+=2;}
        if(bytes.empty()||bytes.size()>size)return 0;uintptr_t found{};
        for(size_t p=0;p<=size-bytes.size();++p){bool equal=true;for(size_t i=0;i<bytes.size();++i)if(bytes[i]>=0&&data[p+i]!=bytes[i]){equal=false;break;}if(equal){if(found)return 0;found=reinterpret_cast<uintptr_t>(begin+p);}}
        return found;
    }
    bool read(uintptr_t address,void* out,size_t count)const{
        if(pristine.empty()||!contains(address,count))return copy(address,out,count);
        std::memcpy(out,pristine.data()+(address-reinterpret_cast<uintptr_t>(begin)),count);return true;
    }
    uintptr_t branch(uintptr_t address)const{
        if(!contains(address,5))return 0;uint8_t opcode{};int32_t displacement{};
        if(!read(address,&opcode,1)||!read(address+1,&displacement,4)||(opcode!=0xe8&&opcode!=0xe9))return 0;
        uintptr_t target=address+5+displacement;return contains(target)?target:0;
    }
    uintptr_t callTarget(uintptr_t address)const{
        uintptr_t target=branch(address);if(!target)return 0;
        uint8_t opcode{};read(target,&opcode,1);if(opcode==0xe9)return branch(target);return target;
    }
};
}
