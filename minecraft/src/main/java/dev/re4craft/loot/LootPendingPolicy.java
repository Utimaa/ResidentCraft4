package dev.re4craft.loot;

/** A focus/coverage hold retains valid suppressed native drops for bounded recovery. */
public final class LootPendingPolicy {
    public static final long MAX_AGE_NANOS=30_000_000_000L;
    public enum Action {DROP,WAIT,READY}
    private LootPendingPolicy() { }
    public static Action decide(boolean currentGeneration,boolean currentEpoch,boolean anchored,
                                boolean requested,boolean ready,long age) {
        if(!currentGeneration||!currentEpoch||!anchored||!requested||age<0||age>MAX_AGE_NANOS)return Action.DROP;
        return ready?Action.READY:Action.WAIT;
    }
}
