package dev.re4craft.loot;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

final class LootMilestonesTest {
    @Test void EveryTierIsOncePerPlayerAcrossSavedReloadAndNativeSessionChanges(){
        UUID first=UUID.randomUUID(),second=UUID.randomUUID();LootMilestones ledger=new LootMilestones();
        for(int tier=0;tier<=5;tier++){assertTrue(ledger.fresh(first,tier));assertTrue(ledger.commit(first,tier));assertFalse(ledger.commit(first,tier));assertTrue(ledger.fresh(second,tier));}
        var reloaded=new LootMilestones(ledger.snapshot());
        for(int tier=0;tier<=5;tier++){assertFalse(reloaded.fresh(first,tier));assertTrue(reloaded.commit(second,tier));}
        assertEquals(63,reloaded.snapshot().getFirst().tiers());assertEquals(2,reloaded.snapshot().size());
    }
    @Test void NoCommitKeepsTheSameGearClaimFreshForSpawnRetry(){
        LootMilestones ledger=new LootMilestones();UUID player=UUID.randomUUID();
        assertTrue(ledger.fresh(player,5));assertTrue(ledger.fresh(player,5));assertTrue(ledger.commit(player,5));assertFalse(ledger.fresh(player,5));
    }
    @Test void BoundedPlayersNeverEvictEarlierClaims(){
        LootMilestones ledger=new LootMilestones();List<UUID> players=new ArrayList<>();
        for(int i=0;i<LootMilestones.MAX_PLAYERS;i++){UUID player=UUID.randomUUID();players.add(player);assertTrue(ledger.canTrack(player));assertTrue(ledger.commit(player,0));}
        UUID extra=UUID.randomUUID();assertFalse(ledger.canTrack(extra));assertFalse(ledger.commit(extra,0));
        for(UUID player:players){assertFalse(ledger.fresh(player,0));assertTrue(ledger.canTrack(player));assertTrue(ledger.commit(player,5));}
        assertEquals(LootMilestones.MAX_PLAYERS,ledger.snapshot().size());
    }
    @Test void SavedInputAndTierClaimsFailClosed(){
        UUID player=UUID.randomUUID();
        for(int mask:new int[]{0,-1,64,128})assertThrows(IllegalArgumentException.class,()->new LootMilestones(List.of(new LootMilestones.Stamp(player,mask))));
        assertThrows(IllegalArgumentException.class,()->new LootMilestones(List.of(new LootMilestones.Stamp(player,1),new LootMilestones.Stamp(player,2))));
        for(int tier:new int[]{-1,6})assertThrows(IllegalArgumentException.class,()->new LootMilestones().commit(player,tier));
    }
}
