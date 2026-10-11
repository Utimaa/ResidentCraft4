package dev.re4craft.loot;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.UUID;

/** Bounded saved book rotation; enemy and crate events cannot skip one another's books. */
public final class LootBookCycles {
    public static final int MAX_PLAYERS=64,BOOKS=7;
    public record Stamp(UUID player,int enemy,int crate){ }
    private final LinkedHashMap<UUID,Stamp> cycles=new LinkedHashMap<>();
    public LootBookCycles(){ }
    public LootBookCycles(List<Stamp> saved){
        if(saved.size()>MAX_PLAYERS)throw new IllegalArgumentException("Unbounded loot book rotations");
        for(var stamp:saved)if(stamp.player()==null||stamp.enemy()<0||stamp.enemy()>=BOOKS||stamp.crate()<0||stamp.crate()>=BOOKS||cycles.putIfAbsent(stamp.player(),stamp)!=null)
            throw new IllegalArgumentException("Invalid saved loot book rotation");
    }
    public int index(UUID player,int source){
        check(player,source);var stamp=cycles.get(player);return stamp==null?0:source==1?stamp.enemy():stamp.crate();
    }
    public boolean canTrack(UUID player){return player!=null&&(cycles.containsKey(player)||cycles.size()<MAX_PLAYERS);}
    public boolean commit(UUID player,int source){
        check(player,source);if(!canTrack(player))return false;
        var stamp=cycles.getOrDefault(player,new Stamp(player,0,0));
        cycles.put(player,new Stamp(player,source==1?(stamp.enemy()+1)%BOOKS:stamp.enemy(),source==2?(stamp.crate()+1)%BOOKS:stamp.crate()));return true;
    }
    public List<Stamp> snapshot(){return List.copyOf(new ArrayList<>(cycles.values()));}
    private static void check(UUID player,int source){if(player==null||source<1||source>2)throw new IllegalArgumentException("Invalid loot book rotation identity");}
}
