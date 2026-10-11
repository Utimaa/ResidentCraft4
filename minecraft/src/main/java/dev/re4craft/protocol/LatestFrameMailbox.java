package dev.re4craft.protocol;

import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.List;
import java.util.Objects;

/** Production socket handoff. Reliable resources precede the latest bounded frames;
 * a socket identity prevents an old writer or producer from affecting a new peer. */
public final class LatestFrameMailbox {
    public enum Slot {STATE,MOTION,BLOCKS,AVATAR,SCENE,ENTITIES,HUD}
    private final ArrayDeque<Wire.Packet> reliable=new ArrayDeque<>(64);
    private final Wire.Packet[] latest=new Wire.Packet[Slot.values().length];
    private Object session;
    public synchronized void begin(Object identity) {
        session=Objects.requireNonNull(identity);clear();notifyAll();
    }
    public synchronized void end(Object identity) {
        if(identity!=session)return;
        session=null;clear();notifyAll();
    }
    public synchronized boolean offerReliable(Object identity,Wire.Packet packet) {
        if(identity==null||identity!=session||reliable.size()>=64)return false;
        reliable.addLast(Objects.requireNonNull(packet));notifyAll();return true;
    }
    public synchronized boolean publish(Object identity,Slot slot,Wire.Packet packet) {
        if(identity==null||identity!=session)return false;
        latest[slot.ordinal()]=Objects.requireNonNull(packet);notifyAll();return true;
    }
    /** Wait without polling or a lost notification. Output writes happen outside this lock. */
    public synchronized boolean awaitDrain(Object identity,List<Wire.Packet> output) throws InterruptedException {
        output.clear();
        while(identity!=null&&identity==session&&!hasData())wait();
        if(identity==null||identity!=session)return false;
        while(!reliable.isEmpty())output.add(reliable.removeFirst());
        for(int i=0;i<latest.length;i++)if(latest[i]!=null){output.add(latest[i]);latest[i]=null;}
        return true;
    }
    private boolean hasData() {
        if(!reliable.isEmpty())return true;
        for(Wire.Packet packet:latest)if(packet!=null)return true;
        return false;
    }
    private void clear(){reliable.clear();Arrays.fill(latest,null);}
}
