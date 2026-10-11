package dev.re4craft.loot;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
final class LootBookCyclesTest {
    @Test void EnemyInterleaveCannotSkipCrateBooksAcrossReloadAndSessionChanges(){
        var cycles=new LootBookCycles();var player=UUID.randomUUID();
        for(int crate=0;crate<16;crate++){
            assertEquals(crate%7,cycles.index(player,2));
            for(int enemy=0;enemy<crate%4;enemy++)assertTrue(cycles.commit(player,1));
            assertEquals(crate%7,cycles.index(player,2));assertTrue(cycles.commit(player,2));
            cycles=new LootBookCycles(cycles.snapshot());
        }
        assertEquals(2,cycles.index(player,2));
        assertEquals(0,cycles.index(UUID.randomUUID(),2));
    }
    @Test void FailedSpawnAndReplayWithoutCommitPreserveRotation(){
        var cycles=new LootBookCycles();var player=UUID.randomUUID();
        assertEquals(0,cycles.index(player,2));assertEquals(0,cycles.index(player,2));assertTrue(cycles.commit(player,2));assertEquals(1,cycles.index(player,2));
    }
    @Test void SavedStateBoundedAndMalformedIndicesFailClosed(){
        var cycles=new LootBookCycles();var player=UUID.randomUUID();
        for(int index:new int[]{-1,7,Integer.MAX_VALUE})assertThrows(IllegalArgumentException.class,()->new LootBookCycles(List.of(new LootBookCycles.Stamp(player,index,0))));
        assertThrows(IllegalArgumentException.class,()->new LootBookCycles(List.of(new LootBookCycles.Stamp(player,0,0),new LootBookCycles.Stamp(player,1,1))));
        for(int count=0;count<64;count++)assertTrue(cycles.commit(UUID.randomUUID(),2));
        assertFalse(cycles.canTrack(player));assertFalse(cycles.commit(player,2));assertEquals(64,cycles.snapshot().size());
    }
}
