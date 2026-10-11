#include "../native/d3d_hook_transaction.hpp"
#include "../native/d3d_device_snapshot.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using namespace re4craft;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Ops {
    std::array<bool,3> created{},enabled{},external{};
    std::array<int,3> creates{},enables{},disables{},removes{};
    int failCreate=-1,failEnable=-1,failDisable=-1,failRemove=-1;
    std::vector<std::string> trace;
    int create(size_t i){++creates[i];trace.push_back("C"+std::to_string(i));if(external[i]||created[i])return 9;if(int(i)==failCreate)return 21;created[i]=true;return 0;}
    int enable(size_t i){++enables[i];trace.push_back("E"+std::to_string(i));check(created[i]&&!external[i],"enabled unowned hook");if(int(i)==failEnable)return 31;enabled[i]=true;return 0;}
    int disable(size_t i){++disables[i];trace.push_back("D"+std::to_string(i));check(created[i]&&!external[i],"disabled pre-existing hook");if(int(i)==failDisable)return 41;enabled[i]=false;return 0;}
    int remove(size_t i){++removes[i];trace.push_back("R"+std::to_string(i));check(created[i]&&!external[i],"removed pre-existing hook");if(int(i)==failRemove)return 51;created[i]=enabled[i]=false;return 0;}
    bool empty()const{return created==std::array<bool,3>{}&&enabled==std::array<bool,3>{};}
};
struct DeviceOps {
    static constexpr uintptr_t slot=0x1000,device=0x2000,tableAddress=0x3000;
    uintptr_t published=device,objectTable=tableAddress;
    std::array<void*,85> table{};
    bool failSlot{},failObject{},failTable{},changePublished{},changeTable{};
    int invalidMethod=-1,readCalls{},executableCalls{};
    DeviceOps(){for(size_t i=0;i<table.size();++i)table[i]=reinterpret_cast<void*>(0x4000+i*16);}
    bool read(uintptr_t address,void* output,size_t size){++readCalls;
        if(address==slot&&size==sizeof(published)&&!failSlot){std::memcpy(output,&published,size);return true;}
        if(address==device&&size==sizeof(objectTable)&&!failObject){std::memcpy(output,&objectTable,size);return true;}
        if(address==tableAddress&&size==sizeof(table)&&!failTable){std::memcpy(output,table.data(),size);return true;}
        return false;
    }
    bool executable(void* address){++executableCalls;
        if(changePublished)published=0x9000;if(changeTable)objectTable=0xA000;
        return address&&!(invalidMethod>=0&&address==table[size_t(invalidMethod)]);
    }
};
void testDeviceAcquisition(){
    static_assert(sizeof(D3DDeviceSnapshot::table)==340,"Production RE4 vtable fixture must use the x86 ABI");
    DeviceOps ops;D3DDeviceSnapshot snapshot{};
    check(acquireD3DDeviceSnapshot(DeviceOps::slot,ops,snapshot),"published device rejected");
    check(snapshot.device==DeviceOps::device&&snapshot.tableAddress==DeviceOps::tableAddress&&snapshot.table==ops.table,"device table copy changed addresses");
    ops.table[42]=nullptr;check(snapshot.table[42]!=nullptr,"snapshot retained mutable borrowed table storage");
    for(int failure=0;failure<8;++failure){
        DeviceOps broken;D3DDeviceSnapshot result=snapshot;
        if(failure==0)broken.published=0;
        if(failure==1)broken.failSlot=true;
        if(failure==2)broken.failObject=true;
        if(failure==3)broken.objectTable=0;
        if(failure==4)broken.failTable=true;
        if(failure==5)broken.changePublished=true;
        if(failure==6)broken.changeTable=true;
        if(failure==7)broken.published=0xB000;
        check(!acquireD3DDeviceSnapshot(DeviceOps::slot,broken,result),"invalid or changing device was accepted");
        check(!result.device&&!result.tableAddress&&result.table==std::array<void*,85>{},"failed acquisition leaked a previous valid snapshot");
    }
    for(size_t index:{size_t(16),size_t(17),size_t(37),size_t(39),size_t(41),size_t(42),size_t(43),size_t(81),size_t(82),size_t(83),size_t(84)}){
        DeviceOps broken;broken.invalidMethod=int(index);check(!acquireD3DDeviceSnapshot(DeviceOps::slot,broken,snapshot),"nonexecutable required method accepted");
        broken.invalidMethod=-1;broken.table[index]=nullptr;check(!acquireD3DDeviceSnapshot(DeviceOps::slot,broken,snapshot),"null required method accepted");
    }
    DeviceOps nullSlot;check(!acquireD3DDeviceSnapshot(0,nullSlot,snapshot)&&nullSlot.readCalls==0,"null slot was read");
}
}
int main(){try{
    testDeviceAcquisition();
    for(int target=0;target<3;++target){
        Ops ops;D3DHookTransaction<3> hooks;ops.failCreate=target;auto result=hooks.install(ops);
        check(!result.installed&&result.cleanupComplete&&result.failedStage==HookStage::Create&&result.index==size_t(target)&&result.status==21,"wrong Create failure diagnostic");
        check(ops.empty(),"Create failure leaked a hook");for(int i=0;i<3;++i)check(ops.removes[i]==(i<target?1:0),"Create rollback adopted failed/unreached target");
        ops.failCreate=-1;check(hooks.install(ops).installed,"Create failure retry failed");check(hooks.removeAll(ops).cleanupComplete&&ops.empty(),"Create retry shutdown leaked");
    }
    for(int target=0;target<3;++target){
        Ops ops;D3DHookTransaction<3> hooks;ops.failEnable=target;auto result=hooks.install(ops);
        check(!result.installed&&result.cleanupComplete&&result.failedStage==HookStage::Enable&&result.index==size_t(target)&&result.status==31,"wrong Enable failure diagnostic");
        check(ops.empty(),"Enable failure left an enabled or created hook");for(int i=0;i<3;++i)check(ops.removes[i]==1&&ops.disables[i]==(i<target?1:0),"Enable rollback wrong ownership");
        ops.failEnable=-1;check(hooks.install(ops).installed,"Enable failure retry failed");size_t calls=ops.trace.size();check(hooks.install(ops).installed&&ops.trace.size()==calls,"successful installation was recreated");
    }
    for(int target=0;target<3;++target){
        Ops ops;D3DHookTransaction<3> hooks;ops.external[target]=true;auto result=hooks.install(ops);
        check(!result.installed&&result.status==9&&result.cleanupComplete,"pre-existing target was adopted");
        check(ops.external[target]&&ops.removes[target]==0&&ops.disables[target]==0&&ops.empty(),"pre-existing hook was modified");
        ops.external[target]=false;check(hooks.install(ops).installed,"unowned conflict recovery failed");
    }
    {
        Ops ops;D3DHookTransaction<3> hooks;ops.failEnable=1;ops.failRemove=2;auto result=hooks.install(ops);
        check(!result.installed&&!result.cleanupComplete&&hooks.owns(2)&&!hooks.owns(0)&&!hooks.owns(1),"cleanup failure ownership lost");
        auto before=ops.creates;result=hooks.install(ops);check(!result.cleanupComplete&&result.failedStage==HookStage::Remove&&ops.creates==before,"retry recreated an uncleared hook");
        ops.failEnable=ops.failRemove=-1;check(hooks.install(ops).installed,"retained cleanup/retry failed");
        ops.failDisable=1;ops.failRemove=1;auto cleanup=hooks.removeAll(ops);check(!cleanup.cleanupComplete&&hooks.owns(1)&&hooks.enabled(1),"live failed-cleanup original ownership lost");
        ops.failDisable=ops.failRemove=-1;check(hooks.removeAll(ops).cleanupComplete&&ops.empty(),"final owned cleanup failed");
    }
    {
        // MinHook RemoveHook also disables an enabled hook. A successful
        // removal completes cleanup even when an explicit disable failed.
        Ops ops;D3DHookTransaction<3> hooks;check(hooks.install(ops).installed,"disable recovery setup failed");
        ops.failDisable=1;size_t before=ops.trace.size();auto cleanup=hooks.removeAll(ops);
        check(cleanup.cleanupComplete&&ops.empty()&&!hooks.owns(1),"successful Remove did not recover failed Disable");
        const std::vector<std::string> expected{"D2","R2","D1","R1","D0","R0"};
        check(std::vector<std::string>(ops.trace.begin()+before,ops.trace.end())==expected,"cleanup is not in reverse creation order");
        ops.failDisable=-1;check(hooks.install(ops).installed,"disable/remove recovery retry failed");
    }
    std::puts("PASS: native table acquisition rejects invalid/changing publications; all three Create/Enable failure positions roll back, retries recover, existing hooks remain unowned, failed cleanup retains ownership and installed calls are idempotent.");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
