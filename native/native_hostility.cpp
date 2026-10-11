#include "native_hostility.hpp"
#include "hook_support.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace re4craft::hostilitydetail {
namespace {
constexpr uint32_t Read=0x40000000,Write=0x80000000,Execute=0x20000000;
constexpr size_t MaxSnapshot=16u*1024u*1024u;
struct View {
    const Image& image;
    const Section* section(uintptr_t address,size_t size) const {
        if(address<image.base||size>image.size)return nullptr;
        uint64_t rva=address-image.base;
        for(const auto& s:image.sections)
            if(rva>=s.rva&&rva-s.rva<=s.bytes.size()&&size<=s.bytes.size()-(rva-s.rva))return &s;
        return nullptr;
    }
    bool read(uintptr_t address,void* destination,size_t size)const {
        auto s=section(address,size);if(!s)return false;
        std::memcpy(destination,s->bytes.data()+address-image.base-s->rva,size);return true;
    }
    bool readOnly(uintptr_t address,size_t size)const {auto s=section(address,size);return s&&(s->flags&Read)&&!(s->flags&(Write|Execute));}
    bool executable(uintptr_t address,size_t size=1)const {auto s=section(address,size);return s&&(s->flags&Execute);}
    bool word(uintptr_t address,uint32_t& value)const{return read(address,&value,4);}
    bool name(uintptr_t descriptor,const char* expected)const {
        const size_t size=std::strlen(expected)+1;auto s=section(descriptor+8,size);
        return s&&(s->flags&Read)&&!std::memcmp(s->bytes.data()+descriptor+8-image.base-s->rva,expected,size);
    }
};
bool validImage(const Image& image){
    if(!image.base||image.size<4096||image.size>128u*1024u*1024u||uint64_t(image.base)+image.size>UINT32_MAX
        ||image.sections.empty()||image.sections.size()>32)return false;
    size_t copied=0;
    for(size_t i=0;i<image.sections.size();++i){const auto& s=image.sections[i];
        if(s.bytes.empty()||!(s.flags&Read)||s.rva>=image.size||s.bytes.size()>image.size-s.rva||s.bytes.size()>MaxSnapshot-copied)return false;
        copied+=s.bytes.size();
        for(size_t n=0;n<i;++n){const auto& other=image.sections[n];
            if(uint64_t(s.rva)<uint64_t(other.rva)+other.bytes.size()&&uint64_t(other.rva)<uint64_t(s.rva)+s.bytes.size())return false;}
    }
    return true;
}
uintptr_t descriptor(const View& view,const char* name){
    size_t size=std::strlen(name)+1;uintptr_t found{};
    for(const auto& s:view.image.sections){
        if(s.flags&Execute||s.bytes.size()<size+8)continue;
        for(size_t i=8;i+size<=s.bytes.size();++i)if(!std::memcmp(s.bytes.data()+i,name,size)){
            // The TypeDescriptor spare word may hold the CRT's cached name;
            // its value is not actor identity. The original mangled name and
            // module type_info vtable supply the actual qualification.
            uintptr_t address=view.image.base+s.rva+i-8;uint32_t typeInfo{},method{};
            if(!view.word(address,typeInfo)||!view.readOnly(typeInfo,4)
                ||!view.word(typeInfo,method)||!view.executable(method))return 0;
            if(found)return 0;found=address;
        }
    }
    return found;
}
bool hierarchy(const View& view,uintptr_t address,uintptr_t descriptor,const char* first){
    uint32_t header[4]{};if(!view.readOnly(address,sizeof(header))||!view.read(address,header,sizeof(header))
        ||header[0]||header[1]||header[2]!=5||!view.readOnly(header[3],20))return false;
    const char* names[]={first,".?AVcEm@@",".?AVcModel@@",".?AVcCoord@@",".?AVcUnit@@"};
    for(size_t i=0;i<5;++i){uint32_t base{},type{};
        if(!view.word(header[3]+i*4,base)||!view.readOnly(base,24)||!view.word(base,type)
            ||!view.name(type,names[i])||(i==0&&type!=descriptor))return false;
    }
    return true;
}
uintptr_t table(const View& view,uintptr_t td,const char* name){
    uintptr_t found{};
    for(const auto& s:view.image.sections){
        if(!(s.flags&Read)||(s.flags&(Write|Execute)))continue;
        for(size_t i=0;i+20<=s.bytes.size();i+=4){uint32_t col[5]{};std::memcpy(col,s.bytes.data()+i,sizeof(col));
            if(col[0]||col[1]||col[2]||col[3]!=td||!hierarchy(view,col[4],td,name))continue;
            uintptr_t locator=view.image.base+s.rva+i;
            for(const auto& v:view.image.sections){
                if(!(v.flags&Read)||(v.flags&(Write|Execute)))continue;
                for(size_t j=0;j+40<=v.bytes.size();j+=4){uint32_t words[10]{};std::memcpy(words,v.bytes.data()+j,sizeof(words));
                    if(words[0]!=locator)continue;
                    bool methods=true;for(size_t k=1;k<10;++k)methods&=view.executable(words[k]);
                    if(!methods)continue;if(found)return 0;found=view.image.base+v.rva+j+4;
                }
            }
        }
    }
    return found;
}
uintptr_t follow(const View& view,uintptr_t branch){
    uint8_t opcode{};int32_t offset{};
    if(!view.executable(branch,5)||!view.read(branch,&opcode,1)||(opcode!=0xe8&&opcode!=0xe9)
        ||!view.read(branch+1,&offset,4))return 0;
    uintptr_t address=branch+5+offset;
    for(int n=0;n<4;++n){if(!view.executable(address,5)||!view.read(address,&opcode,1))return 0;
        if(opcode!=0xe9)return address;
        if(!view.read(address+1,&offset,4))return 0;address=address+5+offset;
    }
    return 0;
}
uintptr_t constructor(const View& view,uintptr_t vtable,uintptr_t& parent){
    uintptr_t found{};parent=0;
    for(const auto& s:view.image.sections){if(!(s.flags&Execute))continue;
        for(size_t i=0;i+18<=s.bytes.size();++i){const uint8_t* p=s.bytes.data()+i;uint32_t table{};std::memcpy(&table,p+10,4);
            if(p[0]!=0x56||p[1]!=0x8b||p[2]!=0xf1||p[3]!=0xe8||p[8]!=0xc7||p[9]!=0x06||table!=vtable
                ||p[14]!=0x8b||p[15]!=0xc6||p[16]!=0x5e||p[17]!=0xc3)continue;
            uintptr_t address=view.image.base+s.rva+i,base=follow(view,address+3);
            if(!base||found)return 0;found=address;parent=base;
        }
    }
    return found;
}
bool baseConstructor(const View& view,uintptr_t address){
    // Exact cEm construction observed at5AF720 in the pinned PE. Wildcards are
    // only its relative base call and relocated base-vtable operand.
    const int pattern[]={0x56,0x8b,0xf1,0xe8,-1,-1,-1,-1,0xd9,0xee,0xc7,0x06,-1,-1,-1,-1,
        0xd9,0x9e,0x28,0x03,0,0,0x32,0xc0,0x66,0xc7,0x86,0x2c,0x03,0,0,0,0,0x88,0x86,0x2e,0x01,0,0,
        0xc7,0x46,0x04,0x21,0,0,0,0x8b,0xc6,0x5e,0xc3};
    std::array<uint8_t,sizeof(pattern)/sizeof(pattern[0])> bytes{};
    if(!view.executable(address,bytes.size())||!view.read(address,bytes.data(),bytes.size()))return false;
    for(size_t i=0;i<bytes.size();++i)if(pattern[i]>=0&&bytes[i]!=pattern[i])return false;
    return true;
}
}
bool resolve(const Image& image,Tables& out){
    out={};if(!validImage(image))return false;View view{image};
    uintptr_t common=descriptor(view,".?AVcEm10@@"),merchant=descriptor(view,".?AVcEm18@@");
    if(!common||!merchant||common==merchant)return false;
    uint32_t commonTypeInfo{},merchantTypeInfo{};
    if(!view.word(common,commonTypeInfo)||!view.word(merchant,merchantTypeInfo)||commonTypeInfo!=merchantTypeInfo)return false;
    Tables result{};result.commonGanado=table(view,common,".?AVcEm10@@");result.merchant=table(view,merchant,".?AVcEm18@@");
    if(!result.commonGanado||!result.merchant||result.commonGanado==result.merchant)return false;
    uintptr_t commonBase{},merchantBase{};
    result.commonConstructor=constructor(view,result.commonGanado,commonBase);
    result.merchantConstructor=constructor(view,result.merchant,merchantBase);
    if(!result.commonConstructor||!result.merchantConstructor||!commonBase||commonBase!=merchantBase||!baseConstructor(view,commonBase))return false;
    result.baseConstructor=commonBase;out=result;return true;
}
}
namespace re4craft {
bool NativeHostility::initialize(LogFn log){
    tables_={};
    try {
        const uintptr_t module=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};
        if(!hookdetail::copy(module,&dos,sizeof(dos))||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||dos.e_lfanew>0x100000
            ||!hookdetail::copy(module+dos.e_lfanew,&nt,sizeof(nt))||nt.Signature!=IMAGE_NT_SIGNATURE
            ||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC
            ||nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER32)||nt.FileHeader.NumberOfSections>32)return false;
        hostilitydetail::Image image{static_cast<uint32_t>(module),nt.OptionalHeader.SizeOfImage,{}};
        if(image.size<4096||image.size>128u*1024u*1024u||uint64_t(image.base)+image.size>UINT32_MAX)return false;
        uintptr_t sections=module+dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;size_t total=0;
        for(unsigned n=0;n<nt.FileHeader.NumberOfSections;++n){IMAGE_SECTION_HEADER header{};
            if(!hookdetail::copy(sections+n*sizeof(header),&header,sizeof(header)))return false;
            size_t count=std::min(header.Misc.VirtualSize,header.SizeOfRawData);
            // Windows may have discarded .reloc after image initialization.
            // Its bytes are not needed by live RTTI/constructor resolution.
            if(!count||!(header.Characteristics&IMAGE_SCN_MEM_READ)||(header.Characteristics&IMAGE_SCN_MEM_DISCARDABLE))continue;
            if(header.VirtualAddress>=image.size||count>image.size-header.VirtualAddress||count>16u*1024u*1024u-total)return false;
            hostilitydetail::Section section{header.VirtualAddress,header.Characteristics,std::vector<uint8_t>(count)};
            if(!hookdetail::copy(module+header.VirtualAddress,section.bytes.data(),count))return false;
            total+=count;image.sections.push_back(std::move(section));
        }
        if(!hostilitydetail::resolve(image,tables_)){if(log)log("Native hostility RTTI/constructor proof unavailable; mixed Ganado/Merchant wolf targeting remains disabled");return false;}
        if(log){char message[256];sprintf_s(message,"Verified actor classes: commonGanado vtable=%08X merchant vtable=%08X (RTTI, hierarchy, methods and distinct constructors)",unsigned(tables_.commonGanado),unsigned(tables_.merchant));log(message);}
        return true;
    } catch(...) {tables_={};if(log)log("Native hostility metadata snapshot failed; optional mixed-kind targeting remains disabled");return false;}
}
}
