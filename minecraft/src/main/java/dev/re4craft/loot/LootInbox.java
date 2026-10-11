package dev.re4craft.loot;

import dev.re4craft.protocol.Wire;
import java.util.ArrayDeque;

/** Bounded FIFO: held native drops keep their original order and identity. */
public final class LootInbox {
    public static final int MAX_PENDING=256;
    public record Event(Wire.Loot loot,long received,int generation) { }
    public record Result(LootPendingPolicy.Action action,Event event) { }
    private final ArrayDeque<Event> pending=new ArrayDeque<>();
    private int generation;
    public synchronized boolean accept(Wire.Loot loot,long now) {
        if(pending.size()>=MAX_PENDING)return false;
        pending.addLast(new Event(loot,now,generation));return true;
    }
    public synchronized void reset(){++generation;pending.clear();}
    public synchronized Result take(int epoch,boolean anchored,boolean requested,boolean ready,long now) {
        Event head=pending.peekFirst();if(head==null)return null;
        var action=LootPendingPolicy.decide(head.generation==generation,head.loot.epoch()==epoch,anchored,requested,ready,now-head.received);
        // WAIT must never rotate the head: the persistent ordered high-water
        // mark would otherwise reject smaller valid ids after a focus hold.
        if(action!=LootPendingPolicy.Action.WAIT)pending.removeFirst();
        return new Result(action,head);
    }
}
