#include "loot_policy.hpp"
#include <array>
#include <cstdio>
#include <stdexcept>

using namespace re4craft;
namespace {
unsigned checks{};
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
}
int main(){try{
    // Independent expected active chapter groups from the original 1.1.0
    // title decoder's table, including every chapter-boundary transition.
    constexpr std::array<int,19> tiers{0,0,0,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5};
    check(nativeLootTier(0,0,0)==0,"chapter 1-1 rejected useful Minecraft loot");
    for(unsigned chapter=0;chapter<256;++chapter){
        const int expected=chapter<tiers.size()?tiers[chapter]:-1;
        check(nativeLootTier(uint8_t(chapter),0,0)==expected,"zero-based native progression boundary mismatch");
        // All unrelated flags may coexist without changing a real campaign
        // tier. Bonus-game selection must still reject Leon, including zero.
        check(nativeLootTier(uint8_t(chapter),0,~NativeBonusGameMask)==expected,"unrelated native state changed campaign tier");
        for(uint32_t bonus:{0x80000000U,0x40000000U,0x1000U,NativeBonusGameMask}){
            check(nativeLootTier(uint8_t(chapter),0,bonus)==-1,"bonus-game Leon received campaign useful loot");
            check(nativeLootTier(uint8_t(chapter),0,bonus|~NativeBonusGameMask)==-1,"bonus-game flag bypassed with other state");
        }
        for(unsigned character=1;character<256;++character)
            check(nativeLootTier(uint8_t(chapter),uint8_t(character),0)==-1,"non-Leon or unknown character received campaign loot");
    }
    // The queue/eligibility remains fail-open: rejection must let the native
    // allocator preserve quest items and stop duplicate useful item rewards.
    check(nativeLootItemEligible(4,10,0,0),"ordinary campaign ammo was not eligible");
    check(nativeLootItemEligible(120,100,0,0),"ordinary campaign money was not eligible");
    for(uint32_t id:{12U,15U,27U,29U,35U,59U,61U,119U,122U,141U,272U})
        check(!nativeLootItemEligible(id,1,0,0),"protected native item was eligible");
    check(!nativeLootItemEligible(4,1,1,0)&&!nativeLootItemEligible(4,1,0,1),"scripted item flags were ignored");
    NativeLootQueue queue;queue.initialize(123);HostLoot accepted{};
    check(queue.append(1,1,uint32_t(nativeLootTier(0,0,0)),1,2,3,accepted),"first chapter Ganado reward was not queued");
    check(accepted.tier==0&&accepted.source==1&&accepted.epoch==1,"first chapter reward identity changed");
    check(queue.append(1,2,0,1,2,3,accepted),"first chapter crate reward was not queued");
    auto pending=queue.take();check(pending.size()==2&&pending[0].eventId==1&&pending[1].eventId==2,"enemy and crate rewards collided");
    check(queue.retry(pending[0])&&queue.retry(pending[0]),"first chapter retry failed");
    check(queue.take().size()==1,"first chapter retry duplicated rewards");
    check(queue.append(1,2,5,1,2,3,accepted)&&accepted.tier==5,"final chapter enchanted gear reward rejected");
    check(!queue.append(1,2,6,1,2,3,accepted),"unsupported gear tier accepted");
    std::printf("Native zero-based loot policy: %u checks passed.\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Native loot policy failed: %s\n",e.what());return 1;}}
