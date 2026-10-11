package dev.re4craft.loot;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;

/** TCP-ordered high-water marks reject all replayed events in retained sessions. */
public final class LootLedger {
    public static final int MAX_SESSIONS=64;
    public record Stamp(long session,int event) { }
    private final LinkedHashMap<Long,Integer> latest=new LinkedHashMap<>();
    public LootLedger() { }
    public LootLedger(List<Stamp> saved) {
        if(saved.size()>MAX_SESSIONS)throw new IllegalArgumentException("Unbounded loot ledger");
        for(Stamp stamp:saved){if(stamp.session==0||stamp.event==0||latest.putIfAbsent(stamp.session,stamp.event)!=null)throw new IllegalArgumentException("Invalid saved loot identity");}
    }
    public boolean fresh(long session,int event) {
        if(session==0||event==0)return false;
        Integer previous=latest.get(session);
        return previous==null||Integer.compareUnsigned(event,previous)>0;
    }
    public boolean commit(long session,int event) {
        if(!fresh(session,event))return false;
        latest.remove(session);latest.put(session,event);
        while(latest.size()>MAX_SESSIONS)latest.remove(latest.keySet().iterator().next());
        return true;
    }
    public List<Stamp> snapshot(){List<Stamp> result=new ArrayList<>(latest.size());latest.forEach((session,event)->result.add(new Stamp(session,event)));return List.copyOf(result);}
}
