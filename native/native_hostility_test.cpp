#include "native_hostility.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <algorithm>
#include <array>
using namespace re4craft;
using namespace re4craft::hostilitydetail;

namespace {
template<class T>T field(const std::vector<uint8_t>& bytes,size_t offset){assert(offset+sizeof(T)<=bytes.size());T value{};std::memcpy(&value,bytes.data()+offset,sizeof(T));return value;}
std::string sha256(const std::vector<uint8_t>& bytes){
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};DWORD size{},written{};
    assert(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0);
    assert(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),4,&written,0)>=0);
    std::vector<uint8_t> object(size);uint8_t digest[32]{};
    assert(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)>=0);
    assert(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0);
    assert(BCryptFinishHash(hash,digest,32,0)>=0);BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    const char* digits="0123456789abcdef";std::string result;for(uint8_t byte:digest){result+=digits[byte>>4];result+=digits[byte&15];}return result;
}
Image mapped(const std::vector<uint8_t>& bytes){
    auto pe=field<uint32_t>(bytes,0x3c);assert(field<uint32_t>(bytes,pe)==IMAGE_NT_SIGNATURE);
    Image image{field<uint32_t>(bytes,pe+24+28),field<uint32_t>(bytes,pe+24+56),{}};
    uint16_t count=field<uint16_t>(bytes,pe+6);size_t start=pe+24+field<uint16_t>(bytes,pe+20);
    for(size_t n=0;n<count;++n){size_t header=start+n*40;uint32_t flags=field<uint32_t>(bytes,header+36);
        uint32_t size=std::min(field<uint32_t>(bytes,header+8),field<uint32_t>(bytes,header+16)),raw=field<uint32_t>(bytes,header+20);
        if(!size||!(flags&IMAGE_SCN_MEM_READ))continue;assert(raw+size<=bytes.size());
        image.sections.push_back({field<uint32_t>(bytes,header+12),flags,{bytes.begin()+raw,bytes.begin()+raw+size}});
    }
    return image;
}
uint8_t* pointer(Image& image,uintptr_t address,size_t size){
    assert(address>=image.base);uint64_t rva=address-image.base;
    for(auto& section:image.sections)if(rva>=section.rva&&rva-section.rva<=section.bytes.size()&&size<=section.bytes.size()-(rva-section.rva))return section.bytes.data()+rva-section.rva;
    assert(false);return nullptr;
}
void put(Image& image,uintptr_t address,uint32_t value){std::memcpy(pointer(image,address,4),&value,4);}
uint32_t get(Image& image,uintptr_t address){uint32_t value{};std::memcpy(&value,pointer(image,address,4),4);return value;}
void fails(Image& image){Tables tables{1,2,3,4,5};assert(!resolve(image,tables));assert(!tables.commonGanado&&!tables.merchant&&!tables.commonConstructor&&!tables.merchantConstructor&&!tables.baseConstructor);}
void relocate(Image& image,const std::vector<uint8_t>& bytes,uint32_t delta){
    uint32_t pe=field<uint32_t>(bytes,0x3c),at=field<uint32_t>(bytes,pe+24+96+5*8),size=field<uint32_t>(bytes,pe+24+96+5*8+4);
    uint32_t used=0;
    while(used<size){uint32_t page=get(image,image.base+at+used),length=get(image,image.base+at+used+4);assert(length>=8&&length<=size-used);
        for(uint32_t n=8;n<length;n+=2){uint16_t entry{};std::memcpy(&entry,pointer(image,image.base+at+used+n,2),2);
            if(entry>>12==IMAGE_REL_BASED_HIGHLOW){uintptr_t address=image.base+page+(entry&0xfff);put(image,address,get(image,address)+delta);}
            else assert(entry>>12==IMAGE_REL_BASED_ABSOLUTE);
        }
        used+=length;
    }
    image.base+=delta;
}
}
int main(int argc,char** argv){
    const char* path=argc>1?argv[1]:R"(C:\Program Files (x86)\Steam\steamapps\common\Resident Evil 4\Bin32\bio4.exe)";
    std::ifstream file(path,std::ios::binary);assert(file);std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
    assert(sha256(bytes)=="19aed4af0ab06a748ff8744d45ac5580fcd6be6b6b7e944b1ab8822a00c8ee4a");
    Image image=mapped(bytes);Tables tables{};assert(resolve(image,tables));
    assert(tables.commonGanado==0xb1035c&&tables.merchant==0xb10ad8&&tables.commonConstructor==0x47c7d0
        &&tables.merchantConstructor==0x4834f0&&tables.baseConstructor==0x5af720);
    for(uint8_t kind:std::array<uint8_t,3>{0x13,0x14,0x1e}){assert(commonGanadoActor(kind,tables.commonGanado,tables));assert(!commonGanadoActor(kind,tables.merchant,tables));assert(!commonGanadoActor(kind,0xb0f7cc,tables));}
    assert(!commonGanadoActor(0x18,tables.commonGanado,tables));assert(!commonGanadoActor(0x21,tables.commonGanado,tables));assert(!commonGanadoActor(0x14,tables.commonGanado,{}));
    assert(knownNativeHostileKind(0x12)&&knownNativeHostileKind(0x22)&&knownNativeHostileKind(0x44));
    for(uint8_t kind:std::array<uint8_t,12>{0x13,0x14,0x1e,0x18,0x21,0x23,0x26,0x27,0x28,0x29,0x4e,0x53})assert(!knownNativeHostileKind(kind));
    auto discarded=image;discarded.sections.erase(std::remove_if(discarded.sections.begin(),discarded.sections.end(),[](const Section& s){return s.flags&IMAGE_SCN_MEM_DISCARDABLE;}),discarded.sections.end());
    Tables withoutRelocations{};assert(resolve(discarded,withoutRelocations));assert(withoutRelocations.commonGanado==tables.commonGanado);
    auto cachedName=image;put(cachedName,0xbf438c+4,0x12340000);put(cachedName,0xbf46b8+4,0x23450000);Tables cached{};assert(resolve(cachedName,cached));
    auto moved=image;relocate(moved,bytes,0x100000);Tables relocated{};assert(resolve(moved,relocated));
    assert(relocated.commonGanado==tables.commonGanado+0x100000&&relocated.merchant==tables.merchant+0x100000);
    auto unknown=image;pointer(unknown,0xbf438c+8,11)[8]='9';fails(unknown); // Actual metadata from an unrelated class cannot pass by layout alone.
    auto constructor=image;pointer(constructor,tables.commonConstructor,1)[0]=0x90;fails(constructor);
    auto wrongBase=image;pointer(wrongBase,tables.baseConstructor+0x27,1)[0]=0x90;fails(wrongBase);
    auto writable=image;for(auto& section:writable.sections)if(tables.commonGanado>=image.base+section.rva&&tables.commonGanado<image.base+section.rva+section.bytes.size())section.flags|=IMAGE_SCN_MEM_WRITE;fails(writable);
    for(size_t n=0;n<9;++n){auto nonCode=image;put(nonCode,tables.commonGanado+n*4,0xbf438c);fails(nonCode);}
    auto malformed=image;put(malformed,0xbe879c+16,0xdeadbeef);fails(malformed);
    auto wrongHierarchy=image;put(wrongHierarchy,0xbe87b4+8,4);fails(wrongHierarchy);
    auto duplicate=image;
    for(auto& section:duplicate.sections)if(!(section.flags&(IMAGE_SCN_MEM_WRITE|IMAGE_SCN_MEM_EXECUTE))&&section.rva==0x70c000){
        size_t where=(section.bytes.size()-128)&~size_t(3);uintptr_t locator=image.base+section.rva+where;
        std::memcpy(section.bytes.data()+where,pointer(image,0xbe879c,20),20);
        put(duplicate,locator+24,static_cast<uint32_t>(locator));
        std::memcpy(pointer(duplicate,locator+28,36),pointer(image,tables.commonGanado,36),36);break;
    }
    fails(duplicate);
    auto overlapping=image;overlapping.sections.push_back(overlapping.sections.front());fails(overlapping);
    auto oversized=image;oversized.size=UINT32_MAX;fails(oversized);
    NativeHostility self;assert(!self.initialize());assert(!self.commonGanadoActor(0x14,tables.commonGanado));assert(!self.merchantActor(tables.merchant));
    std::cout<<"Native hostility fixture passed: actual hash-pinned RTTI/classes/constructors, relocated PE, mixed Ganado vs Merchant/neutral discrimination, malformed/ambiguous metadata and fail-closed optional initialization.\n";
}
