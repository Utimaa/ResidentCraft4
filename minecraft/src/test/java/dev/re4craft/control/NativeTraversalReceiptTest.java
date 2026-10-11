package dev.re4craft.control;

import dev.re4craft.protocol.Wire;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class NativeTraversalReceiptTest {
    private static final int FLAGS=3|Wire.INPUT_HOST_AUTHORITY|Wire.INPUT_NATIVE_TRAVERSAL;
    private static final long NOW=2_000_000_000L;
    private static final UUID PLAYER=UUID.fromString("00000000-0000-0000-0000-000000000001");
    private static final Object SERVER=new Object(), DIMENSION="owned-overworld";
    private static NativeTraversalReceipt.Scope scope(){return new NativeTraversalReceipt.Scope(7,42,PLAYER,DIMENSION,SERVER,19);}
    private static final NativeTraversalReceipt.Point TARGET=new NativeTraversalReceipt.Point(4,61,-3);
    private static NativeTraversalReceipt.Receipt receipt(){return new NativeTraversalReceipt.Receipt(scope(),TARGET,TARGET,NOW-50_000_000);}
    private static boolean ack(NativeTraversalReceipt.Scope scope,NativeTraversalReceipt.Point target,
                               NativeTraversalReceipt.Point feet,NativeTraversalReceipt.Receipt receipt) {
        return NativeTraversalReceipt.acknowledge(scope,target,feet,receipt,FLAGS,true,true,true,NOW,NOW-10_000_000);
    }
    @Test void actualClientAndServerFeetAcknowledgeSameLanding(){assertTrue(ack(scope(),TARGET,TARGET,receipt()));}
    @Test void echoOfRequestedTargetWithoutServerReceiptIsNotAnAcknowledgment(){assertFalse(ack(scope(),TARGET,TARGET,null));}
    @Test void interpolatedClientFeetMustHaveReachedLanding(){
        assertFalse(ack(scope(),TARGET,new NativeTraversalReceipt.Point(4,64,-3),receipt()));
        assertTrue(ack(scope(),TARGET,new NativeTraversalReceipt.Point(4,61.0625,-3),receipt()));
        assertFalse(ack(scope(),TARGET,new NativeTraversalReceipt.Point(4,61.0625001,-3),receipt()));
    }
    @Test void realServerFeetMustHaveReachedLandingToo(){
        assertFalse(ack(scope(),TARGET,TARGET,new NativeTraversalReceipt.Receipt(scope(),TARGET,new NativeTraversalReceipt.Point(4,62,-3),NOW)));
    }
    @Test void receiptCannotBeReusedForAnotherTargetOrDepartedAndReturnedTarget(){
        assertFalse(ack(scope(),new NativeTraversalReceipt.Point(4,60,-3),TARGET,receipt()));
        var current=new NativeTraversalReceipt.Scope(7,42,PLAYER,DIMENSION,SERVER,20);
        assertFalse(ack(current,TARGET,TARGET,receipt()));
    }
    @Test void epochReconnectPlayerDimensionAndServerReplacementInvalidateReceipt(){
        var old=scope();
        for(var current:new NativeTraversalReceipt.Scope[]{
                new NativeTraversalReceipt.Scope(8,42,PLAYER,DIMENSION,SERVER,19),
                new NativeTraversalReceipt.Scope(7,43,PLAYER,DIMENSION,SERVER,19),
                new NativeTraversalReceipt.Scope(7,0,PLAYER,DIMENSION,SERVER,19),
                new NativeTraversalReceipt.Scope(7,42,UUID.randomUUID(),DIMENSION,SERVER,19),
                new NativeTraversalReceipt.Scope(7,42,PLAYER,"nether",SERVER,19),
                new NativeTraversalReceipt.Scope(7,42,PLAYER,DIMENSION,new Object(),19),
                new NativeTraversalReceipt.Scope(7,42,PLAYER,DIMENSION,null,19)})
            assertFalse(ack(current,TARGET,TARGET,new NativeTraversalReceipt.Receipt(old,TARGET,TARGET,NOW)));
    }
    @Test void staleAndFutureReceiptsFailClosed(){
        assertTrue(ack(scope(),TARGET,TARGET,new NativeTraversalReceipt.Receipt(scope(),TARGET,TARGET,NOW-250_000_000)));
        assertFalse(ack(scope(),TARGET,TARGET,new NativeTraversalReceipt.Receipt(scope(),TARGET,TARGET,NOW-250_000_001)));
        assertFalse(ack(scope(),TARGET,TARGET,new NativeTraversalReceipt.Receipt(scope(),TARGET,TARGET,NOW+1)));
    }
    @Test void inputWatchdogPauseDisconnectAndDeathDisableAcknowledgment(){
        for(int flags:new int[]{0,3,7,19,23,35,51,63,FLAGS|8,FLAGS|64})
            assertFalse(NativeTraversalReceipt.acknowledge(scope(),TARGET,TARGET,receipt(),flags,true,true,true,NOW,NOW));
        assertFalse(NativeTraversalReceipt.acknowledge(scope(),TARGET,TARGET,receipt(),FLAGS,false,true,true,NOW,NOW));
        assertFalse(NativeTraversalReceipt.acknowledge(scope(),TARGET,TARGET,receipt(),FLAGS,true,false,true,NOW,NOW));
        assertFalse(NativeTraversalReceipt.acknowledge(scope(),TARGET,TARGET,receipt(),FLAGS,true,true,false,NOW,NOW));
        assertFalse(NativeTraversalReceipt.acknowledge(scope(),TARGET,TARGET,receipt(),FLAGS,true,true,true,NOW,NOW-750_000_000));
        assertFalse(NativeTraversalReceipt.acknowledge(scope(),TARGET,TARGET,receipt(),FLAGS,true,true,true,NOW,NOW+1));
    }
    @Test void nonfiniteOrUnboundedAndCombinedAxisErrorCannotAcknowledge(){
        for(double v:new double[]{Double.NaN,Double.POSITIVE_INFINITY,30_000_001})
            assertFalse(ack(scope(),TARGET,new NativeTraversalReceipt.Point(v,61,-3),receipt()));
        assertFalse(ack(scope(),TARGET,new NativeTraversalReceipt.Point(4.05,61.05,-3),receipt()));
    }
}
