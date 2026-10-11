package dev.re4craft.protocol;

import java.io.IOException;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class NativeTraversalWireTest {
    private static Wire.Packet input(int flags) {
        byte[] bytes=Wire.buffer(80).putInt(flags).putInt(0).putInt(0).putFloat(0).putFloat(0)
                .putInt(-1).putInt(0).putInt(42).putDouble(4).putDouble(0).putDouble(-3)
                .putInt(1280).putInt(720).putFloat(.5f).putFloat(.5f).putInt(0).putInt(0).array();
        return new Wire.Packet(Wire.INPUT,1,bytes);
    }
    @Test void traversalRequiresOnlyNativeAuthorityWithoutChangingWireLayout() throws Exception {
        var packet=input(39);assertEquals(80,packet.payload().length);assertEquals(6,Wire.VERSION);
        var value=Wire.input(packet);assertTrue(value.active());assertEquals(39,value.flags());
        assertEquals(4,value.hostX());assertEquals(42,value.hostEpoch());
    }
    @Test void traversalWithoutAuthorityOrWithGuestAuthorityIsRejected(){
        for(int flags:new int[]{32,35,48,51,55,63,103})assertThrows(IOException.class,()->Wire.input(input(flags)));
    }
    @Test void ordinaryExistingAuthoritiesAndPausedNativeTraversalRemainRepresentable() throws Exception {
        for(int flags:new int[]{0,3,7,19,39,47})assertEquals(flags,Wire.input(input(flags)).flags());
        assertFalse(Wire.input(input(47)).active());
    }
}
