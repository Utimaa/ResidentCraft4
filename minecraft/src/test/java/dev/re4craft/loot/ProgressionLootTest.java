package dev.re4craft.loot;

import java.util.List;
import dev.re4craft.protocol.Wire;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class ProgressionLootTest {
    @Test void ThreeHeldEventsStayOrderedAndAllCommitAfterResume() {
        LootInbox inbox=new LootInbox();LootLedger ledger=new LootLedger();
        for(int id=1;id<=3;++id)assertTrue(inbox.accept(new Wire.Loot(7,id,2,0,91,1,0,1),0));
        for(int tick=0;tick<17;++tick){var held=inbox.take(7,true,true,false,tick*50_000_000L);assertEquals(LootPendingPolicy.Action.WAIT,held.action());assertEquals(1,held.event().loot().eventId());}
        for(int id=1;id<=3;++id){var event=inbox.take(7,true,true,true,900_000_000L);assertEquals(id,event.event().loot().eventId());assertTrue(ledger.commit(91,id));}
        assertNull(inbox.take(7,true,true,true,1_000_000_000L));
    }
    @Test void ResetCannotConsumeFreshGenerationOrOldEpochEvent() {
        LootInbox inbox=new LootInbox();inbox.accept(new Wire.Loot(7,1,1,0,2,0,0,0),0);inbox.reset();
        inbox.accept(new Wire.Loot(8,2,1,0,2,0,0,0),10);
        assertEquals(2,inbox.take(8,true,true,true,20).event().loot().eventId());
        inbox.accept(new Wire.Loot(7,3,1,0,2,0,0,0),20);
        assertEquals(LootPendingPolicy.Action.DROP,inbox.take(8,true,true,true,30).action());
    }
    @Test void SuppressedNativeLootWaitsThroughFocusAndCoverageHolds() {
        assertEquals(LootPendingPolicy.Action.WAIT,LootPendingPolicy.decide(true,true,true,true,false,25_000_000_000L));
        assertEquals(LootPendingPolicy.Action.READY,LootPendingPolicy.decide(true,true,true,true,true,25_000_000_000L));
        assertEquals(LootPendingPolicy.Action.DROP,LootPendingPolicy.decide(true,false,true,true,false,1));
        assertEquals(LootPendingPolicy.Action.DROP,LootPendingPolicy.decide(false,true,true,true,true,1));
        assertEquals(LootPendingPolicy.Action.DROP,LootPendingPolicy.decide(true,true,true,false,true,1));
        assertEquals(LootPendingPolicy.Action.DROP,LootPendingPolicy.decide(true,true,true,true,true,LootPendingPolicy.MAX_AGE_NANOS+1));
    }
    @Test void EveryChapterDropsOnlyUsefulBoundedCraftingResources(){
        String[] primary={"oak_log","cobblestone","iron_ingot","diamond","netherite_scrap","netherite_scrap"};
        for(int tier=0;tier<=5;tier++)for(int source=1;source<=2;source++)for(int event=1;event<=100;event++){
            var rewards=ProgressionLoot.select(source,tier,71,event);
            assertEquals(rewards,ProgressionLoot.select(source,tier,71,event));
            String resource="minecraft:"+primary[tier];assertTrue(rewards.stream().anyMatch(s->s.item().equals(resource)));
            assertTrue(rewards.size()<=ProgressionLoot.MAX_STACKS);assertTrue(rewards.stream().allMatch(s->s.count()>0&&s.count()<=64));
            assertTrue(rewards.stream().noneMatch(s->s.item().matches(".*_(sword|pickaxe|axe|helmet|chestplate|leggings|boots)$")||s.item().equals("minecraft:bow")||s.item().equals("minecraft:netherite_ingot")));
            if(source==2)assertTrue(rewards.stream().anyMatch(s->s.item().equals("minecraft:bread")||s.item().equals("minecraft:cooked_beef")));
            else assertTrue(rewards.stream().anyMatch(s->s.item().equals("minecraft:arrow")));
            if(tier<3)assertTrue(rewards.stream().noneMatch(s->s.item().contains("diamond")||s.item().contains("netherite")));
            if(tier<4)assertTrue(rewards.stream().noneMatch(s->s.item().equals("minecraft:enchanted_book")));
        }
        assertThrows(IllegalArgumentException.class,()->ProgressionLoot.select(1,6,1,1));
        assertThrows(IllegalArgumentException.class,()->ProgressionLoot.select(0,0,1,1));
    }
    @Test void SixteenLateCratesFundVanillaCraftingSmithingAndEnchantingFromAnyBookRotation(){
        for(int firstBook=0;firstBook<7;firstBook++){
            java.util.Map<String,Integer> counts=new java.util.HashMap<>();java.util.Map<String,Integer> books=new java.util.HashMap<>();
            for(int crate=0;crate<16;crate++)for(var stack:ProgressionLoot.select(2,4,29,crate*37+1,(firstBook+crate)%7)){
                counts.merge(stack.item(),stack.count(),Integer::sum);
                if(stack.item().equals("minecraft:enchanted_book")){
                    String role=stack.enchantments().stream().anyMatch(e->e.id().equals("feather_falling"))?"boots":stack.enchantments().getFirst().id();
                    books.merge(role,1,Integer::sum);
                }
            }
            assertTrue(counts.get("minecraft:diamond")>=29);
            assertTrue(counts.get("minecraft:netherite_scrap")>=24&&counts.get("minecraft:gold_ingot")>=24);
            assertTrue(counts.get("minecraft:netherite_upgrade_smithing_template")>=6);
            assertTrue(counts.get("minecraft:iron_ingot")>=95); // Three anvils plus smithing table.
            assertTrue(counts.get("minecraft:oak_log")*4>=12&&counts.get("minecraft:string")>=3);
            assertTrue(counts.get("minecraft:experience_bottle")*3>=1380); // Real bottle minimum, seven separate fresh anvil operations.
            assertTrue(books.get("protection")>=3&&books.get("boots")>=1&&books.get("sharpness")>=1&&books.get("efficiency")>=1&&books.get("power")>=1);
        }
    }
    @Test void StoredBookDefinitionsAreBoundedCompatibleAndDoNotGrantEquipment(){
        var books=ProgressionLoot.books();assertEquals(7,books.size());
        for(var book:books){assertEquals("minecraft:enchanted_book",book.item());assertEquals(1,book.count());assertTrue(book.enchantments().size()>=3&&book.enchantments().size()<=4);}
        var bow=books.getLast();assertTrue(bow.enchantments().contains(new ProgressionLoot.Enchantment("infinity",1)));
        assertTrue(bow.enchantments().stream().noneMatch(e->e.id().equals("mending")));
        assertThrows(IllegalArgumentException.class,()->new ProgressionLoot.Enchantment("binding_curse",1));
        assertThrows(IllegalArgumentException.class,()->new ProgressionLoot.Stack("minecraft:enchanted_book",2,bow.enchantments()));
        assertThrows(IllegalArgumentException.class,()->new ProgressionLoot.Stack("minecraft:enchanted_book",1,List.of(new ProgressionLoot.Enchantment("power",5),new ProgressionLoot.Enchantment("power",1))));
    }
    @Test void PersistentLedgerRejectsReplayAcrossEpochsAndUnsignedEventBoundary() {
        LootLedger ledger=new LootLedger();assertTrue(ledger.commit(123,1));assertFalse(ledger.commit(123,1));assertFalse(ledger.commit(123,0));
        assertTrue(ledger.commit(123,Integer.MAX_VALUE));assertTrue(ledger.commit(123,Integer.MIN_VALUE));assertFalse(ledger.commit(123,Integer.MAX_VALUE));
        LootLedger reloaded=new LootLedger(ledger.snapshot());assertFalse(reloaded.fresh(123,1));assertTrue(reloaded.commit(123,-1));assertFalse(reloaded.fresh(123,Integer.MIN_VALUE));
        assertTrue(reloaded.commit(124,1));assertEquals(2,reloaded.snapshot().size());
    }
    @Test void LedgerMemoryAndSavedInputAreBounded() {
        LootLedger ledger=new LootLedger();for(int i=1;i<=200;++i)ledger.commit(i,1);assertEquals(64,ledger.snapshot().size());
        assertFalse(ledger.fresh(200,1));assertTrue(ledger.fresh(200,2));
        assertThrows(IllegalArgumentException.class,()->new LootLedger(List.of(new LootLedger.Stamp(1,1),new LootLedger.Stamp(1,2))));
    }
}

