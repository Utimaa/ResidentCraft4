package dev.re4craft.loot;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.UUID;

/** Legacy alpha gear-claim metadata retained for decoding only; material loot ignores it. */
public final class LootMilestones {
    public static final int MAX_PLAYERS=64,VALID_MASK=(1<<(ProgressionLoot.MAX_TIER+1))-1;
    public record Stamp(UUID player,int tiers){ }
    private final LinkedHashMap<UUID,Integer> claims=new LinkedHashMap<>();
    public LootMilestones(){ }
    public LootMilestones(List<Stamp> saved){
        if(saved.size()>MAX_PLAYERS)throw new IllegalArgumentException("Unbounded loot milestones");
        for(Stamp stamp:saved)if(stamp.player()==null||stamp.tiers()<=0||(stamp.tiers()&~VALID_MASK)!=0||claims.putIfAbsent(stamp.player(),stamp.tiers())!=null)
            throw new IllegalArgumentException("Invalid saved loot milestone");
    }
    public boolean fresh(UUID player,int tier){check(player,tier);return (claims.getOrDefault(player,0)&(1<<tier))==0;}
    public boolean commit(UUID player,int tier){
        if(!fresh(player,tier))return false;
        // Never evict a real player's claims and accidentally grant duplicates.
        if(!claims.containsKey(player)&&claims.size()==MAX_PLAYERS)return false;
        claims.put(player,claims.getOrDefault(player,0)|(1<<tier));return true;
    }
    public boolean canTrack(UUID player){return player!=null&&(claims.containsKey(player)||claims.size()<MAX_PLAYERS);}
    public List<Stamp> snapshot(){List<Stamp> result=new ArrayList<>();claims.forEach((player,tiers)->result.add(new Stamp(player,tiers)));return List.copyOf(result);}
    private static void check(UUID player,int tier){if(player==null||tier<0||tier>ProgressionLoot.MAX_TIER)throw new IllegalArgumentException("Invalid loot milestone");}
}
