#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include "beamng_ui_reader.hpp"
#include "../protocol/re4craft_protocol.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>

namespace re4craft {
namespace {
template<class T> T copied(const volatile uint8_t* pointer){
    T value{};auto* bytes=reinterpret_cast<uint8_t*>(&value);
    for(size_t i=0;i<sizeof(T);++i)bytes[i]=pointer[i];
    return value;
}
uint32_t lockValue(const volatile uint8_t* pointer){
    return *reinterpret_cast<const volatile uint32_t*>(pointer);
}
bool boundedHud(const std::vector<uint8_t>& bytes,uint64_t sequence){
    if(bytes.size()<sizeof(Hud)||bytes.size()>UiMappingCapacity)return false;
    Hud h{};std::memcpy(&h,bytes.data(),sizeof(h));
    if(!h.width||h.width>1920||!h.height||h.height>1080||(h.flags!=0&&h.flags!=2)||h.frame!=uint32_t(sequence))return false;
    const size_t pixels=size_t(h.width)*h.height;
    if(!h.flags)return bytes.size()==sizeof(h)+pixels*4;
    size_t at=sizeof(h),filled=0;
    while(at<bytes.size()){
        if(bytes.size()-at<4)return false;
        uint32_t token{};std::memcpy(&token,bytes.data()+at,4);at+=4;
        const size_t count=token&0x7fffffffu;
        if(!count||count>pixels-filled)return false;
        const size_t length=(token&0x80000000u)?4:count*4;
        if(length>bytes.size()-at)return false;
        at+=length;filled+=count;
    }
    return filled==pixels;
}
uint64_t creationTime(HANDLE process){
    FILETIME created{},ended{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&ended,&kernel,&user))return 0;
    return uint64_t(created.dwLowDateTime)|(uint64_t(created.dwHighDateTime)<<32);
}
DWORD socketOwner(SOCKET socket){
    sockaddr_in local{},peer{};int length=sizeof(local);
    if(getsockname(socket,reinterpret_cast<sockaddr*>(&local),&length)||length!=sizeof(local))return 0;
    length=sizeof(peer);
    if(getpeername(socket,reinterpret_cast<sockaddr*>(&peer),&length)||length!=sizeof(peer)
       ||local.sin_family!=AF_INET||peer.sin_family!=AF_INET
       ||local.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)||peer.sin_addr.s_addr!=htonl(INADDR_LOOPBACK))return 0;
    constexpr DWORD maxBytes=2u*1024u*1024u,maxRows=65536;
    DWORD bytes=0;DWORD status=GetExtendedTcpTable(nullptr,&bytes,FALSE,AF_INET,TCP_TABLE_OWNER_PID_ALL,0);
    if(status!=ERROR_INSUFFICIENT_BUFFER||bytes<sizeof(DWORD)||bytes>maxBytes)return 0;
    std::vector<uint8_t> storage(bytes);
    status=GetExtendedTcpTable(storage.data(),&bytes,FALSE,AF_INET,TCP_TABLE_OWNER_PID_ALL,0);
    if(status!=NO_ERROR||bytes>storage.size())return 0;
    auto* table=reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(storage.data());
    if(table->dwNumEntries>maxRows||sizeof(DWORD)+size_t(table->dwNumEntries)*sizeof(MIB_TCPROW_OWNER_PID)>bytes)return 0;
    DWORD owner=0;
    for(DWORD i=0;i<table->dwNumEntries;++i){const auto& row=table->table[i];
        if(row.dwState==MIB_TCP_STATE_ESTAB&&row.dwLocalAddr==peer.sin_addr.s_addr&&row.dwRemoteAddr==local.sin_addr.s_addr
           &&row.dwLocalPort==DWORD(peer.sin_port)&&row.dwRemotePort==DWORD(local.sin_port)){
            if(owner||!row.dwOwningPid)return 0;
            owner=row.dwOwningPid;
        }
    }
    return owner;
}
}
UiReadStatus readUiMapping(const volatile uint8_t* mapping,size_t size,uint32_t owner,uint64_t creation,
                          uint64_t now,uint64_t last,uint64_t after,bool active,UiCapture& result){
    result={};
    if(!mapping||size!=UiMappingSize||!owner||!creation||now>UiMappingMaxCounter)return UiReadStatus::Header;
    const auto h=copied<UiMappingHeader>(mapping);
    if(h.magic!=UiMappingMagic||h.version!=UiMappingVersion||h.owner!=owner||h.capacity!=UiMappingCapacity
       ||h.creation!=creation||!h.producer||h.flags||h.reserved[0]||h.reserved[1]||h.reserved[2]
       ||h.sequence>UiMappingMaxCounter)return UiReadStatus::Header;
    if(!h.sequence)return UiReadStatus::Empty;
    if(h.sequence<last)return UiReadStatus::Regressed;
    const size_t offset=UiMappingHeaderSize+(h.sequence&1)*UiMappingStride;
    const uint32_t before=lockValue(mapping+offset);
    if(before&1)return UiReadStatus::Writing;
    MemoryBarrier();
    const auto s=copied<UiMappingSlot>(mapping+offset);
    if(s.lock!=before)return UiReadStatus::Replaced;
    MemoryBarrier();
    if(lockValue(mapping+offset)!=before||copied<uint64_t>(mapping+24)!=h.sequence)return UiReadStatus::Replaced;
    if(s.sequence!=h.sequence||s.reserved>1||s.length<16||s.length>UiMappingCapacity||s.stamp>UiMappingMaxCounter)return UiReadStatus::Slot;
    if(s.stamp>now)return UiReadStatus::Future;
    if(now-s.stamp>UiMappingMaxAge)return UiReadStatus::Stale;
    if(s.stamp<after)return UiReadStatus::BeforeReset;
    if(!active)return UiReadStatus::Inactive;
    if(h.sequence==last)return UiReadStatus::Unchanged;
    result.payload.resize(s.length);
    // The seqlock protects the immutable copied packet; the reader never maps
    // writable pages or modifies another process's data, even on x86.
    std::memcpy(result.payload.data(),const_cast<const uint8_t*>(mapping+offset+UiMappingSlotHeaderSize),s.length);
    MemoryBarrier();
    const auto latest=copied<uint64_t>(mapping+24);
    if(lockValue(mapping+offset)!=before||latest!=h.sequence){result={};return UiReadStatus::Replaced;}
    if(!boundedHud(result.payload,h.sequence)){result={};return UiReadStatus::Hud;}
    result.sequence=h.sequence;result.stamp=s.stamp;result.screenOpen=s.reserved==1;return UiReadStatus::Frame;
}
struct BeamngUiReader::Impl {
    HANDLE process{},mapping{};const volatile uint8_t* view{};
    uint32_t pid{},epoch{};uint64_t created{},last{},after{},retryOwner{},retryMap{};
    bool initialized{},active{};
    void closeMap(){if(view)UnmapViewOfFile(const_cast<const uint8_t*>(view));view=nullptr;if(mapping)CloseHandle(mapping);mapping=nullptr;last=0;}
    void clear(){closeMap();if(process)CloseHandle(process);process=nullptr;pid=0;created=0;last=after=retryOwner=retryMap=0;epoch=0;initialized=active=false;}
    bool bind(SOCKET socket,uint64_t now){
        if(now<retryOwner)return false;retryOwner=now+1000;
        const DWORD owner=socketOwner(socket);if(!owner)return false;
        HANDLE handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,owner);if(!handle)return false;
        wchar_t filename[32768]{};DWORD length=DWORD(std::size(filename));
        const uint64_t generation=creationTime(handle);
        const bool queried=QueryFullProcessImageNameW(handle,0,filename,&length)!=0;
        const wchar_t* leaf=queried?wcsrchr(filename,L'\\'):nullptr;
        if(!generation||!leaf||_wcsicmp(leaf+1,L"BeamNG.drive.x64.exe")||WaitForSingleObject(handle,0)!=WAIT_TIMEOUT){CloseHandle(handle);return false;}
        // Cross-check that the socket still belongs to this process after the
        // process handle was opened, rather than trusting a reusable PID alone.
        if(socketOwner(socket)!=owner){CloseHandle(handle);return false;}
        process=handle;pid=owner;created=generation;return true;
    }
};
BeamngUiReader::BeamngUiReader():impl_(std::make_unique<Impl>()){}
BeamngUiReader::~BeamngUiReader(){reset();}
void BeamngUiReader::reset(){impl_->clear();}
uint32_t BeamngUiReader::owner() const{return impl_->pid;}
bool BeamngUiReader::poll(uintptr_t connectedSocket,uint32_t epoch,bool active,uint64_t now,UiCapture& result){
    result={};auto& p=*impl_;
    if(!p.initialized||p.epoch!=epoch||p.active!=active){p.after=now;p.last=0;p.epoch=epoch;p.active=active;p.initialized=true;}
    // The retained process HANDLE names an immutable process generation even
    // if its numeric PID is later reused; its FILETIME was checked at bind.
    if(p.process&&WaitForSingleObject(p.process,0)!=WAIT_TIMEOUT)p.clear();
    if(!p.process&&!p.bind(SOCKET(connectedSocket),now))return false;
    if(!p.view){
        if(now<p.retryMap)return false;p.retryMap=now+1000;
        const auto name=L"Local\\RE4CraftUI-"+std::to_wstring(p.pid);
        p.mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name.c_str());if(!p.mapping)return false;
        p.view=static_cast<const volatile uint8_t*>(MapViewOfFile(p.mapping,FILE_MAP_READ,0,0,UiMappingSize));
        if(!p.view){p.closeMap();return false;}
    }
    UiReadStatus status=UiReadStatus::Empty;
    for(unsigned attempt=0;attempt<3;++attempt){
        status=readUiMapping(p.view,UiMappingSize,p.pid,p.created,now,p.last,p.after,active&&epoch,result);
        if(status!=UiReadStatus::Writing&&status!=UiReadStatus::Replaced)break;
    }
    if(status==UiReadStatus::Frame){p.last=result.sequence;return true;}
    if(status==UiReadStatus::Header||status==UiReadStatus::Slot||status==UiReadStatus::Hud
       ||status==UiReadStatus::Stale||status==UiReadStatus::Regressed||status==UiReadStatus::Empty)p.closeMap();
    return false;
}
}
