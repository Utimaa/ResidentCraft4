package dev.re4craft.control;

import dev.re4craft.protocol.Wire;
import java.util.Objects;
import java.util.UUID;

/** A traversal acknowledgment requires two actual positions in one owned scope. */
public final class NativeTraversalReceipt {
    public static final long INPUT_FRESH_NANOS=750_000_000L, RECEIPT_FRESH_NANOS=250_000_000L;
    public static final double POSITION_TOLERANCE=.0625;
    public record Point(double x,double y,double z) { }
    public record Scope(int generation,int epoch,UUID player,Object dimension,Object server,long serial) { }
    /** Constructed by the integrated server after its tick, from the real player's feet. */
    public record Receipt(Scope scope,Point target,Point observed,long appliedNanos) { }
    private NativeTraversalReceipt() { }
    public static boolean traversalInput(int flags) {
        return (flags&Wire.INPUT_NATIVE_TRAVERSAL)!=0
                && (flags&Wire.INPUT_HOST_AUTHORITY)!=0 && (flags&Wire.INPUT_GUEST_AUTHORITY)==0
                && (flags&~63)==0;
    }
    public static boolean sameScope(Scope a,Scope b) {
        return a!=null && b!=null && a.epoch()!=0 && a.player()!=null && a.dimension()!=null && a.server()!=null
                && a.generation()==b.generation() && a.epoch()==b.epoch() && a.serial()==b.serial()
                && a.player().equals(b.player()) && Objects.equals(a.dimension(),b.dimension()) && a.server()==b.server();
    }
    public static boolean close(Point a,Point b) {
        if(a==null || b==null)return false;
        for(double coordinate:new double[]{a.x(),a.y(),a.z(),b.x(),b.y(),b.z()})
            if(!Double.isFinite(coordinate)||Math.abs(coordinate)>30_000_000)return false;
        double x=a.x()-b.x(),y=a.y()-b.y(),z=a.z()-b.z();
        return x*x+y*y+z*z<=POSITION_TOLERANCE*POSITION_TOLERANCE;
    }
    public static boolean acknowledge(Scope current,Point target,Point exportedFeet,Receipt receipt,
                                      int inputFlags,boolean active,boolean connected,boolean alive,
                                      long now,long lastInputNanos) {
        if(!active || !connected || !alive || !traversalInput(inputFlags) || (inputFlags&3)!=3 || (inputFlags&8)!=0
                || now<lastInputNanos || now-lastInputNanos>=INPUT_FRESH_NANOS || receipt==null
                || now<receipt.appliedNanos() || now-receipt.appliedNanos()>RECEIPT_FRESH_NANOS
                || !sameScope(current,receipt.scope()) || !Objects.equals(target,receipt.target()))return false;
        return close(exportedFeet,target) && close(receipt.observed(),target);
    }
}
