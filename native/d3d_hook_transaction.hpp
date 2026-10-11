#pragma once
#include <array>
#include <cstddef>

namespace re4craft {
enum class HookStage {None,Create,Enable,Remove};
struct HookTransactionResult {
    bool installed{},cleanupComplete{true};
    HookStage failedStage{HookStage::None};
    size_t index{};
    int status{};
};

/** Own only hooks this transaction created; failed startup is safely retryable.
 * Ops exposes create/enable/disable/remove(size_t)->int with success==0.
 * Target indices must remain bound to the same functions until cleanup is complete.
 * Original pointers must stay valid for an owned hook until remove succeeds.
 */
template<size_t Count> class D3DHookTransaction {
    static_assert(Count>0,"A hook transaction needs targets");
    std::array<bool,Count> created_{},enabled_{};
public:
    bool owns(size_t index)const{return index<Count&&created_[index];}
    bool enabled(size_t index)const{return index<Count&&enabled_[index];}
    bool installed()const{
        for(size_t i=0;i<Count;++i)if(!created_[i]||!enabled_[i])return false;
        return true;
    }
    template<class Ops> HookTransactionResult removeAll(Ops& ops){
        HookTransactionResult result{};result.index=Count;
        for(size_t n=Count;n>0;--n){size_t i=n-1;if(!created_[i])continue;
            if(enabled_[i]&&ops.disable(i)==0)enabled_[i]=false;
            int status=ops.remove(i);
            if(status==0){created_[i]=enabled_[i]=false;}
            else if(result.cleanupComplete){result.cleanupComplete=false;result.failedStage=HookStage::Remove;result.index=i;result.status=status;}
        }
        return result;
    }
    template<class Ops> HookTransactionResult install(Ops& ops){
        if(installed())return {true,true,HookStage::None,Count,0};
        // A prior cleanup failure keeps explicit ownership. Never recreate a
        // remaining target and accidentally adopt an already-existing hook.
        auto cleaned=removeAll(ops);if(!cleaned.cleanupComplete)return cleaned;
        for(size_t i=0;i<Count;++i){int status=ops.create(i);if(status!=0){auto rollback=removeAll(ops);return {false,rollback.cleanupComplete,HookStage::Create,i,status};}created_[i]=true;}
        for(size_t i=0;i<Count;++i){int status=ops.enable(i);if(status!=0){auto rollback=removeAll(ops);return {false,rollback.cleanupComplete,HookStage::Enable,i,status};}enabled_[i]=true;}
        return {true,true,HookStage::None,Count,0};
    }
};
}
