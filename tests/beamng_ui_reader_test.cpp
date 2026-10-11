#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "../native/beamng_ui_reader.hpp"
#include "../native/transport.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace re4craft;
namespace {
unsigned checks{};
void check(bool okay,const char* description){++checks;if(!okay)throw std::runtime_error(description);}
uint64_t creation(HANDLE process){FILETIME a{},b{},c{},d{};check(GetProcessTimes(process,&a,&b,&c,&d)!=0,"actual process FILETIME");return uint64_t(a.dwLowDateTime)|(uint64_t(a.dwHighDateTime)<<32);}
struct Mapping {
    HANDLE handle{};uint8_t* bytes{};
    Mapping(uint32_t owner,uint64_t created){
        const auto name=L"Local\\RE4CraftUI-"+std::to_wstring(owner);
        handle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(UiMappingSize),name.c_str());check(handle&&GetLastError()!=ERROR_ALREADY_EXISTS,"isolated new mapping");
        bytes=static_cast<uint8_t*>(MapViewOfFile(handle,FILE_MAP_WRITE|FILE_MAP_READ,0,0,UiMappingSize));check(bytes!=nullptr,"fixture writer mapping");
        UiMappingHeader h{UiMappingMagic,UiMappingVersion,owner,uint32_t(UiMappingCapacity),created,0,GetCurrentProcessId(),0,{}};std::memcpy(bytes,&h,sizeof(h));
    }
    ~Mapping(){if(bytes)UnmapViewOfFile(bytes);if(handle)CloseHandle(handle);}
    void publish(uint64_t sequence,uint64_t stamp,bool screenOpen=false){
        const size_t at=64+(sequence&1)*UiMappingStride;auto* lock=reinterpret_cast<volatile LONG*>(bytes+at);
        InterlockedIncrement(lock);MemoryBarrier();
        UiMappingSlot slot{uint32_t(*lock),48,sequence,stamp,screenOpen?1u:0u};std::memcpy(bytes+at+4,reinterpret_cast<uint8_t*>(&slot)+4,28);
        Hud h{4,2,0,uint32_t(sequence)};std::memcpy(bytes+at+32,&h,16);
        for(unsigned i=0;i<8;++i){uint8_t color[4]{uint8_t(sequence&127),16,32,128};std::memcpy(bytes+at+48+i*4,color,4);}
        MemoryBarrier();InterlockedIncrement(lock);InterlockedExchange64(reinterpret_cast<volatile LONG64*>(bytes+24),LONG64(sequence));
    }
};
void pureMapping(){
    const auto pid=GetCurrentProcessId();const auto created=creation(GetCurrentProcess());Mapping writer(pid,created);
    HANDLE handle=OpenFileMappingW(FILE_MAP_READ,FALSE,(L"Local\\RE4CraftUI-"+std::to_wstring(pid)).c_str());check(handle!=nullptr,"readonly mapping handle");
    auto* readOnly=static_cast<const volatile uint8_t*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,UiMappingSize));check(readOnly!=nullptr,"actual x86 readonly view");
    UiCapture packet{};const uint64_t now=GetTickCount64();
    auto read=[&](uint64_t last=0,uint64_t after=0,bool active=true){return readUiMapping(readOnly,UiMappingSize,pid,created,now,last,after,active,packet);};
    check(read()==UiReadStatus::Empty,"empty mapping is never a frame");writer.publish(1,now-150);
    check(read()==UiReadStatus::Frame&&packet.stamp==now-150&&packet.payload.size()==48&&!packet.screenOpen,"exact play capture age/mode/payload preserved");
    check(read(1)==UiReadStatus::Unchanged,"no duplicate frame freshness");check(read(0,now)==UiReadStatus::BeforeReset,"old capture cannot cross activation");
    check(read(0,0,false)==UiReadStatus::Inactive&&packet.payload.empty(),"inactive metadata copies no RGBA");
    check(readUiMapping(readOnly,UiMappingSize,pid+1,created,now,0,0,true,packet)==UiReadStatus::Header,"foreign owner rejected");
    check(readUiMapping(readOnly,UiMappingSize,pid,created+1,now,0,0,true,packet)==UiReadStatus::Header,"same PID wrong creation rejected");
    *reinterpret_cast<uint32_t*>(writer.bytes+4)=1;check(read()==UiReadStatus::Header,"old untyped mapping version rejected");
    *reinterpret_cast<uint32_t*>(writer.bytes+4)=UiMappingVersion;
    writer.publish(2,now,true);check(read()==UiReadStatus::Frame&&packet.screenOpen,"original picker capture mode preserved");
    const size_t two=64+(2&1)*UiMappingStride;
    *reinterpret_cast<uint64_t*>(writer.bytes+two+24)=2;check(read()==UiReadStatus::Slot&&!packet.screenOpen&&packet.payload.empty(),"unknown capture mode rejected without stale picker state");
    writer.publish(3,now,false);check(read()==UiReadStatus::Frame&&!packet.screenOpen,"play frame cannot retain previous picker mode");
    writer.publish(2,now-501);check(read()==UiReadStatus::Stale,"actual capture timeout");writer.publish(3,now+1);check(read()==UiReadStatus::Future,"racing future capture rejected without refresh");
    writer.publish(4,now);const size_t at=64+(4&1)*UiMappingStride;
    *reinterpret_cast<uint32_t*>(writer.bytes+at)|=1;check(read()==UiReadStatus::Writing,"odd seqlock cannot publish partial packet");
    *reinterpret_cast<uint32_t*>(writer.bytes+at)+=1;
    *reinterpret_cast<uint32_t*>(writer.bytes+at+4)=uint32_t(UiMappingCapacity+1);check(read()==UiReadStatus::Slot,"oversize payload rejected");
    writer.publish(5,now);const size_t five=64+(5&1)*UiMappingStride;
    *reinterpret_cast<uint32_t*>(writer.bytes+five+32+12)=6;check(read()==UiReadStatus::Hud,"wire frame must equal actual sequence");
    writer.publish(7,now);check(read(8)==UiReadStatus::Regressed,"helper generation sequence regression detected");
    for(uint64_t sequence:(std::initializer_list<uint64_t>{0xffffffffULL,0x100000000ULL,0x100000001ULL})){
        writer.publish(sequence,now);check(read()==UiReadStatus::Frame&&packet.sequence==sequence,"x86 source64 counter/uint32 frame wrap exact");
    }
    UnmapViewOfFile(const_cast<const uint8_t*>(readOnly));CloseHandle(handle);
}
void seqlockRace(){
    const auto pid=GetCurrentProcessId();const auto created=creation(GetCurrentProcess());Mapping writer(pid,created);
    HANDLE handle=OpenFileMappingW(FILE_MAP_READ,FALSE,(L"Local\\RE4CraftUI-"+std::to_wstring(pid)).c_str());
    auto* view=static_cast<const volatile uint8_t*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,UiMappingSize));check(view!=nullptr,"full-frame readonly race view");
    std::atomic<bool> finished=false;std::thread source([&]{
        for(uint64_t sequence=0xffffffd0ULL;sequence<0x100000070ULL;++sequence){
            const size_t at=64+(sequence&1)*UiMappingStride;auto* lock=reinterpret_cast<volatile LONG*>(writer.bytes+at);
            InterlockedIncrement(lock);MemoryBarrier();
            UiMappingSlot slot{uint32_t(*lock),uint32_t(UiMappingCapacity),sequence,GetTickCount64(),sequence&1};std::memcpy(writer.bytes+at+4,reinterpret_cast<uint8_t*>(&slot)+4,28);
            Hud hud{1920,1080,0,uint32_t(sequence)};std::memcpy(writer.bytes+at+32,&hud,16);
            std::memset(writer.bytes+at+48,int(sequence&255),UiMappingCapacity-16);
            MemoryBarrier();InterlockedIncrement(lock);InterlockedExchange64(reinterpret_cast<volatile LONG64*>(writer.bytes+24),LONG64(sequence));Sleep(2);
        }
        finished=true;
    });
    unsigned frames=0,races=0;bool valid=true;uint64_t last=0;
    while(!finished){UiCapture packet{};const auto status=readUiMapping(view,UiMappingSize,pid,created,GetTickCount64(),last,0,true,packet);
        races+=status==UiReadStatus::Writing||status==UiReadStatus::Replaced;
        if(status==UiReadStatus::Frame){
            Hud h{};std::memcpy(&h,packet.payload.data(),16);const uint8_t expected=uint8_t(packet.sequence&255);
            valid&=h.frame==uint32_t(packet.sequence)&&packet.screenOpen==bool(packet.sequence&1)&&std::all_of(packet.payload.begin()+16,packet.payload.end(),[&](uint8_t value){return value==expected;});
            frames++;last=packet.sequence;
        }
    }
    source.join();UnmapViewOfFile(const_cast<const uint8_t*>(view));CloseHandle(handle);
    check(valid&&frames>0&&races>0,"1920x1080 seqlock races never mix capture mode/pixels across64bitcounter wrap");
    std::printf("Actual x86 readonly full-frame seqlock race: %u complete 8294416-byte packets, %u safely rejected races.\n",frames,races);
}
void historicalUiMapping(){
    const std::filesystem::path path("build/beamng-ui-qa-live3/original-ui-hud4-recovered-final.bin");
    if(!std::filesystem::exists(path)){std::puts("SKIP: historical actual UI packet is unavailable in this checkout.");return;}
    const auto fileSize=std::filesystem::file_size(path);check(fileSize>=16&&fileSize<=UiMappingCapacity,"historical genuine Hud4 packet bounds");const auto size=size_t(fileSize);
    std::ifstream stream(path,std::ios::binary);std::vector<uint8_t> pixels(size);stream.read(reinterpret_cast<char*>(pixels.data()),std::streamsize(size));check(bool(stream),"read exact historical original Hud4 packet");
    Hud hud{};std::memcpy(&hud,pixels.data(),sizeof(hud));check(hud.frame>0,"historical source frame is nonzero");
    const auto pid=GetCurrentProcessId();const auto created=creation(GetCurrentProcess());Mapping writer(pid,created);const auto now=GetTickCount64();
    const size_t at=64+(hud.frame&1)*UiMappingStride;
    UiMappingSlot slot{2,uint32_t(size),hud.frame,now,0};std::memcpy(writer.bytes+at,&slot,sizeof(slot));std::memcpy(writer.bytes+at+32,pixels.data(),size);
    MemoryBarrier();InterlockedExchange64(reinterpret_cast<volatile LONG64*>(writer.bytes+24),LONG64(hud.frame));
    HANDLE handle=OpenFileMappingW(FILE_MAP_READ,FALSE,(L"Local\\RE4CraftUI-"+std::to_wstring(pid)).c_str());check(handle!=nullptr,"historical readonly version2 mapping handle");
    auto* view=static_cast<const volatile uint8_t*>(MapViewOfFile(handle,FILE_MAP_READ,0,0,UiMappingSize));check(view!=nullptr,"historical exact pixels readonly mapping");
    UiCapture packet{};check(readUiMapping(view,UiMappingSize,pid,created,now,0,0,true,packet)==UiReadStatus::Frame&&!packet.screenOpen&&packet.payload==pixels,"version2 mode does not change genuine captured Hud4 bytes");
    *reinterpret_cast<uint64_t*>(writer.bytes+at+24)=1;
    check(readUiMapping(view,UiMappingSize,pid,created,now,0,0,true,packet)==UiReadStatus::Frame&&packet.screenOpen&&packet.payload==pixels,"mode receipt preserves exact historical original pixels");
    UnmapViewOfFile(const_cast<uint8_t*>(view));CloseHandle(handle);
    std::printf("PASS: historical genuine %ux%u Hud4 bytes unchanged through version2 readonly mapping; replay fixture is not a fresh in-game capture.\n",hud.width,hud.height);
}
int server(){
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(listener==INVALID_SOCKET)return 2;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
    if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))||listen(listener,1))return 3;
    int length=sizeof(address);getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length);
    std::printf("%u\n",unsigned(ntohs(address.sin_port)));std::fflush(stdout);
    SOCKET peer=accept(listener,nullptr,nullptr);closesocket(listener);if(peer==INVALID_SOCKET)return 4;
    u_long nonblocking=1;ioctlsocket(peer,FIONBIO,&nonblocking);
    const auto pid=GetCurrentProcessId();const auto created=creation(GetCurrentProcess());
    auto mapping=std::make_unique<Mapping>(pid,created);auto start=GetTickCount64(),last=uint64_t{};uint64_t sequence=0xfffffffdULL;bool restarted=false;
    while(GetTickCount64()-start<6000){
        char incoming[4096];int received=recv(peer,incoming,sizeof(incoming),0);if(received==0)break;if(received<0&&WSAGetLastError()!=WSAEWOULDBLOCK)break;
        const auto now=GetTickCount64(),elapsed=now-start;
        if(elapsed>=900&&elapsed<1700)mapping.reset();
        if(elapsed>=1700&&!restarted){mapping=std::make_unique<Mapping>(pid,created);sequence=0;restarted=true;}
        if(mapping&&now-last>=20){++sequence;mapping->publish(sequence,now-150,bool(sequence&1));last=now;}
        Sleep(1);
    }
    mapping.reset();shutdown(peer,SD_BOTH);closesocket(peer);return 0;
}
struct Child {
    PROCESS_INFORMATION process{};HANDLE output{};uint16_t port{};
    Child(){
        const auto executable=std::filesystem::absolute("build/beamng-ui-reader-tests/BeamNG.drive.x64.exe").wstring();
        std::wstring command=L"\""+executable+L"\" --server";
        HANDLE write{};SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};check(CreatePipe(&output,&write,&attributes,0)!=0,"fixture process output pipe");SetHandleInformation(output,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;startup.hStdOutput=write;startup.hStdError=write;startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        const BOOL started=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);CloseHandle(write);check(started!=0,"bounded synthetic BeamNG-named TCP fixture process");
        std::string line;const auto begin=GetTickCount64();
        while(line.find('\n')==std::string::npos&&GetTickCount64()-begin<3000){DWORD available{};if(PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)&&available){char bytes[128];DWORD n{};check(ReadFile(output,bytes,std::min<DWORD>(available,sizeof(bytes)),&n,nullptr)!=0,"fixture port output");line.append(bytes,n);}else Sleep(1);}
        const auto value=std::stoul(line);check(value>1024&&value<=65535,"actual ephemeral loopback fixture port");port=uint16_t(value);
    }
    ~Child(){if(process.hProcess){if(WaitForSingleObject(process.hProcess,1000)==WAIT_TIMEOUT)TerminateProcess(process.hProcess,9);CloseHandle(process.hProcess);}if(process.hThread)CloseHandle(process.hThread);if(output)CloseHandle(output);}
};
void liveReader(){
    Child child;SOCKET connection=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(connection!=INVALID_SOCKET,"actual reader client socket");sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(child.port);
    check(connect(connection,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"connect actual synthetic peer");
    BeamngUiReader reader;UiCapture packet{};bool first=false,restarted=false;unsigned frames{};const auto begin=GetTickCount64();double maximumWarm=0;
    while(GetTickCount64()-begin<3100){
        const auto now=GetTickCount64();auto start=std::chrono::steady_clock::now();const bool received=reader.poll(uintptr_t(connection),7,true,now,packet);
        const auto cost=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();if(now-begin>200)maximumWarm=std::max(maximumWarm,cost);
        if(received){
            check(reader.owner()==child.process.dwProcessId&&reader.owner()!=GetCurrentProcessId(),"owner is actual reversed server tuple, not client PID");
            check(GetTickCount64()>=packet.stamp+150,"150ms source capture age preserved");
            Hud h{};std::memcpy(&h,packet.payload.data(),16);check(h.frame==uint32_t(packet.sequence)&&packet.screenOpen==bool(packet.sequence&1)&&packet.payload[16]==uint8_t(packet.sequence&127),"x86 immutable complete mode/pixel seqlock packet");
            first=true;restarted|=now-begin>1700&&packet.sequence<100;frames++;
        }
        Sleep(2);
    }
    check(first&&frames>3,"actual TCP owner/mapping source frames");check(restarted,"stale old helper mapping released and fresh sequence accepted");
    std::printf("Actual x86 reader synthetic TCP/map fixture: %u frames, max warm poll %.3fms; old generation released/reopened.\n",frames,maximumWarm);
    reader.reset();shutdown(connection,SD_BOTH);closesocket(connection);
}
void transportWorker(){
    Child child;Transport peer({},true);Input input{};input.hostEpoch=9;input.flags=Enabled;peer.setInput(input);peer.start(child.port);
    bool accepted=false,sawRetained=false,lastScreenOpen=false;const auto begin=GetTickCount64();
    while(GetTickCount64()-begin<2600){
        peer.setInput(input);auto updates=peer.takeUpdates();
        if(updates.newHud){check(updates.connected&&updates.hudValid&&updates.hudAgeMs>=145,"worker publishes genuine capture age, not read/decode time");check(updates.hud.rgba.size()==32,"production Hud4 decoder used by worker");check(updates.hudUiModeKnown&&updates.hudUiScreenOpen==bool(updates.hud.frame&1),"worker retains captured source mode with exact immutable HUD pixels");lastScreenOpen=updates.hudUiScreenOpen;accepted=true;}
        else if(accepted&&updates.hudValid){check(updates.hudUiModeKnown&&updates.hudUiScreenOpen==lastScreenOpen,"one-shot HUD move does not erase retained view ownership");sawRetained=true;}
        Sleep(10);
    }
    check(accepted&&sawRetained,"native transport-thread original UI integration and retained view receipt");input.flags=0;peer.setInput(input);auto disabled=peer.takeUpdates();check(!disabled.hudValid&&!disabled.newHud&&!disabled.hudUiModeKnown&&!disabled.hudUiScreenOpen,"activation reset clears pending UI and source mode immediately");
    input.hostEpoch++;input.flags=Enabled;peer.setInput(input);auto epoch=peer.takeUpdates();check(!epoch.hudValid&&!epoch.newHud&&!epoch.hudUiModeKnown&&!epoch.hudUiScreenOpen,"epoch reset cannot retain a decoded old capture/mode");peer.stop();
}
void actualUi(uint32_t expectedPid,uint16_t port,const char* path,unsigned samples=10){
    SOCKET connection=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(connection!=INVALID_SOCKET,"actual UI proof socket");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
    check(connect(connection,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"actual owned UI endpoint connection");
    BeamngUiReader reader;UiCapture packet{};unsigned frames=0,restarts=0;uint64_t previous{};const auto begin=GetTickCount64();double maxPoll=0;uint64_t minAge=500,maxAge=0;
    while(GetTickCount64()-begin<(samples==10?15000:120000)&&frames<samples){
        auto started=std::chrono::steady_clock::now();const bool received=reader.poll(uintptr_t(connection),1,true,GetTickCount64(),packet);
        const double cost=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        if(reader.owner())check(reader.owner()==expectedPid,"actual UI socket belongs to the exact owned QA head");
        if(received){
            if(previous&&packet.sequence<previous)++restarts;previous=packet.sequence;
            Transport decoder({},true);check(decoder.accept(Message::Hud,packet.payload),"actual stock originalHud4 production decode");auto decoded=decoder.takeUpdates();check(decoded.newHud,"actual stock originalHud4 mailbox");
            const auto age=GetTickCount64()-packet.stamp;check(age<=500,"actual originalUI capture freshness");minAge=std::min(minAge,age);maxAge=std::max(maxAge,age);maxPoll=std::max(maxPoll,cost);
            std::ofstream out(path,std::ios::binary);check(bool(out),"save exact original Hud4 packet");out.write(reinterpret_cast<const char*>(packet.payload.data()),std::streamsize(packet.payload.size()));check(bool(out),"write exact original Hud4 packet");
            std::printf("OriginalUI native frame=%u owner=%u sequence=%llu captureAge=%llums read=%.3fms size=%zu dimensions=%ux%u\n",++frames,reader.owner(),static_cast<unsigned long long>(packet.sequence),static_cast<unsigned long long>(age),cost,packet.payload.size(),decoded.hud.width,decoded.hud.height);std::fflush(stdout);
        }
        Sleep(20);
    }
    reader.reset();shutdown(connection,SD_BOTH);closesocket(connection);check(frames==samples,"actual original UI frame count within bounded capture window");
    std::printf("PASS: %u untouched original Hud4 captures from actual owned head%u, native readonly map/reverse socket PID+FILETIME and production decoder; capture age %llu..%llums, maximum packet read %.3fms, same-head producer restarts=%u.\n",frames,expectedPid,static_cast<unsigned long long>(minAge),static_cast<unsigned long long>(maxAge),maxPoll,restarts);
}
}
int main(int argc,char** argv){
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 2;
    try {if(argc==2&&std::strcmp(argv[1],"--server")==0){int result=server();WSACleanup();return result;}
        if((argc==5||argc==6)&&std::strcmp(argv[1],"--read-actual-ui")==0){const auto pid=std::stoul(argv[2]),port=std::stoul(argv[3]);const auto samples=argc==6?std::stoul(argv[5]):10;
            check(pid>0&&pid<=0xffffffffUL&&port>1024&&port<=65535&&samples>0&&samples<=300,"actual owned UI proof PID/port/sample bounds");actualUi(uint32_t(pid),uint16_t(port),argv[4],unsigned(samples));WSACleanup();return 0;}
        pureMapping();seqlockRace();historicalUiMapping();liveReader();transportWorker();std::printf("PASS: %u x86 original UI checks; actual TCP/process/readonly mapping identity, stable seqlock, source age, restart and transport activation/epoch bounds. Runtime fixture geometry/UI is synthetic; historical packet replay is separately labelled.\n",checks);WSACleanup();return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Original UI reader fixture failed: %s\n",error.what());WSACleanup();return 1;}
}
