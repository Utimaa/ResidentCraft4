package dev.re4craft.world;

import static org.junit.jupiter.api.Assertions.*;
import java.io.IOException;
import java.nio.ByteBuffer;
import dev.re4craft.protocol.Wire;
import org.junit.jupiter.api.Test;

class HostMovingSurfaceTest {
    private static final long SOURCE=0x102030405060708L;
    private static HostTerrain floor(int revision,float x,float y,float z) throws IOException {
        ByteBuffer b=Wire.buffer(32+2*48);b.putInt(7).putInt(2).putInt(5).putInt(revision).putFloat(0).putFloat(0).putFloat(0).putFloat(64);
        for(float value:new float[]{x-4,y,z-4,x-4,y,z+4,x+4,y,z-4,x+4,y,z-4,x-4,y,z+4,x+4,y,z+4})b.putFloat(value);
        b.putLong(SOURCE).putInt(0).putLong(SOURCE).putInt(1);return HostTerrain.decode(b.array());
    }
    private static byte[] motion(int epoch,int revision,long tick,int flags,float... matrix) {
        ByteBuffer b=Wire.buffer(84);b.putInt(epoch).putInt(revision).putInt(1).putInt(0).putLong(tick).putLong(SOURCE).putInt(flags);for(float v:matrix)b.putFloat(v);return b.array();
    }
    private static float[] translation(float x,float y,float z) {return new float[]{1,0,0,x,0,1,0,y,0,0,1,z};}
    @Test void standingRiderFollowsTranslationAndFreshCollider() throws Exception {
        HostTerrain base=floor(1,0,0,0);var support=base.support(1,0,1);assertNotNull(support);
        HostTerrain next=base.moving(motion(7,1,50,0,translation(2,1,3)));
        assertArrayEquals(new double[]{2,1,3},next.carry(support,1,0,1,true,-.08),1e-6);
        var hit=next.cast(3,2,4,3,0,4);assertNotNull(hit);assertEquals(1,hit.y(),1e-6);
        assertNull(next.cast(-3,2,-3,-3,-2,-3));
        var movedSupport=next.support(3,1,4);assertNotNull(movedSupport);
        HostTerrain later=next.moving(motion(7,1,100,0,translation(3,2,4)));
        assertArrayEquals(new double[]{1,1,1},later.carry(movedSupport,3,1,4,true,0),1e-6);
        assertArrayEquals(new double[]{0,0,0},later.carry(later.support(4,2,5),4,2,5,true,0),1e-6);
    }
    @Test void stationaryPlayerAndMobContactRotateAroundSurfaceOrigin() throws Exception {
        HostTerrain base=floor(1,0,0,0);HostTerrain next=base.moving(motion(7,1,50,0,new float[]{0,0,-1,3,0,1,0,2,1,0,0,4}));
        // Same source physics supports a stationary Steve and an off-centre mob.
        for(double[] rider:new double[][]{{1,0,2},{-2,0,1}}){var support=base.support(rider[0],rider[1],rider[2]);assertNotNull(support);
            assertArrayEquals(new double[]{3-rider[2]-rider[0],2,4+rider[0]-rider[2]},next.carry(support,rider[0],rider[1],rider[2],true,-.08),1e-6);}
    }
    @Test void disembarkJumpAirborneAndRemovedRegistrationStopCarry() throws Exception {
        HostTerrain base=floor(1,0,0,0);var support=base.support(1,0,1);HostTerrain next=base.moving(motion(7,1,50,0,translation(2,1,0)));
        assertNull(next.carry(support,1,0,1,false,-.08));assertNull(next.carry(support,1,0,1,true,.42));
        assertNull(next.carry(support,2,0,1,true,-.08));assertNull(next.carry(support,1,1,1,true,-.08));
        HostTerrain removed=base.moving(motion(7,1,100,1,new float[12]));assertNull(removed.carry(support,1,0,1,true,0));assertNull(removed.cast(1,1,1,1,-1,1));
        assertNull(base.moving(motion(7,1,50,0,translation(9,0,0))).carry(support,1,0,1,true,0));
    }
    @Test void fullRecaptureRetainsContactAcrossBaselineChange() throws Exception {
        HostTerrain old=floor(1,0,0,0).moving(motion(7,1,50,0,translation(1,1,0)));var support=old.support(2,1,1);
        HostTerrain fresh=floor(2,2,2,0);assertArrayEquals(new double[]{1,1,0},fresh.carry(support,2,1,1,true,0),1e-6);
    }
    @Test void staleForeignAndMalformedTransformsCannotMoveTerrain() throws Exception {
        HostTerrain base=floor(1,0,0,0);HostTerrain moved=base.moving(motion(7,1,100,0,translation(1,0,0)));
        assertSame(moved,moved.moving(motion(7,1,99,0,translation(9,0,0))));
        assertSame(moved,moved.moving(motion(8,1,101,0,translation(9,0,0))));assertSame(moved,moved.moving(motion(7,2,101,0,translation(9,0,0))));
        assertThrows(IOException.class,()->base.moving(new byte[23]));assertThrows(IOException.class,()->base.moving(motion(7,1,50,0,new float[12])));
        float[] invalid=translation(0,0,0);invalid[0]=Float.NaN;assertThrows(IOException.class,()->base.moving(motion(7,1,50,0,invalid)));
        byte[] unknown=motion(7,1,50,0,translation(0,0,0));ByteBuffer.wrap(unknown).order(java.nio.ByteOrder.LITTLE_ENDIAN).putLong(24,9);assertThrows(IOException.class,()->base.moving(unknown));
    }
    @Test void emptyMovingReceiptRestoresOriginalGeometry() throws Exception {
        HostTerrain base=floor(1,0,0,0);HostTerrain moved=base.moving(motion(7,1,50,0,translation(1,1,0)));
        ByteBuffer reset=Wire.buffer(24);reset.putInt(7).putInt(1).putInt(0).putInt(0).putLong(100);
        HostTerrain restored=moved.moving(reset.array());assertEquals(0,restored.cast(0,2,0,0,-2,0).y(),1e-6);
    }
    @Test void originalWireReadsMovingFramesAndSourceIdentifiedCollision() throws Exception {
        byte[] moving=motion(7,1,50,0,translation(2,1,3));java.io.ByteArrayOutputStream stream=new java.io.ByteArrayOutputStream();
        Wire.write(stream,new Wire.Packet(Wire.MOVING_SURFACES,1,moving));Wire.Packet decoded=Wire.read(new java.io.ByteArrayInputStream(stream.toByteArray()));assertEquals(Wire.MOVING_SURFACES,decoded.type());assertArrayEquals(moving,decoded.payload());
        ByteBuffer terrain=Wire.buffer(80);terrain.putInt(7).putInt(1).putInt(5).putInt(1).putFloat(0).putFloat(0).putFloat(0).putFloat(64);for(float v:new float[]{-4,0,-4,-4,0,4,4,0,-4})terrain.putFloat(v);terrain.putLong(SOURCE).putInt(0);
        stream.reset();Wire.write(stream,new Wire.Packet(Wire.COLLISION,2,terrain.array()));decoded=Wire.read(new java.io.ByteArrayInputStream(stream.toByteArray()));assertEquals(1,HostTerrain.decode(decoded.payload()).size());
    }
}
