package dev.re4craft.world;

import static org.junit.jupiter.api.Assertions.*;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import org.junit.jupiter.api.Test;

class HostTerrainTest {
    private static byte[] frame(int epoch,float[]... triangles) {
        ByteBuffer output=ByteBuffer.allocate(32+triangles.length*36).order(ByteOrder.LITTLE_ENDIAN);
        output.putInt(epoch).putInt(triangles.length).putInt(1).putInt(0).putFloat(0).putFloat(0).putFloat(0).putFloat(128);
        for(float[] triangle:triangles)for(float value:triangle)output.putFloat(value);
        return output.array();
    }
    @Test void floorHitAndWindingBothSides() throws IOException {
        var terrain=HostTerrain.decode(frame(7,new float[]{0,0,0,0,0,10,10,0,0}));
        var down=terrain.cast(2,4,2,2,-4,2);assertNotNull(down);
        assertEquals(0.5,down.t(),1e-9);assertEquals(0,down.y(),1e-9);assertEquals(1,down.ny(),1e-9);
        var up=terrain.cast(2,-4,2,2,4,2);assertNotNull(up);assertEquals(-1,up.ny(),1e-9);
        assertEquals(7,terrain.epoch());assertNull(terrain.cast(9,4,9,9,-4,9));
    }
    @Test void nearestTriangleWinsAndNegativeSpatialCellsWork() throws IOException {
        var terrain=HostTerrain.decode(frame(3,
                new float[]{-100,0,-100,-100,0,-80,-80,0,-100},
                new float[]{-100,2,-100,-100,2,-80,-80,2,-100}));
        var hit=terrain.cast(-96,5,-96,-96,-1,-96);assertNotNull(hit);assertEquals(2,hit.y(),1e-9);assertEquals(0.5,hit.t(),1e-9);
    }
    @Test void wallAndSlopeKeepRealSurfaceNormals() throws IOException {
        var wall=HostTerrain.decode(frame(1,new float[]{0,0,0,0,8,0,0,0,8}));
        var hit=wall.cast(4,1,1,-4,1,1);assertNotNull(hit);assertEquals(1,hit.nx(),1e-9);
        var slope=HostTerrain.decode(frame(1,new float[]{0,0,0,0,0,10,10,5,0}));
        hit=slope.cast(2,5,2,2,-5,2);assertNotNull(hit);assertEquals(1,hit.y(),1e-9);assertTrue(hit.ny()>0.8);assertTrue(hit.nx()<0);
    }
    @Test void malformedFramesAreRejectedWithoutAllocation() {
        assertThrows(IOException.class,()->HostTerrain.decode(new byte[15]));
        byte[] count=frame(1);ByteBuffer.wrap(count).order(ByteOrder.LITTLE_ENDIAN).putInt(4,Integer.MAX_VALUE);
        assertThrows(IOException.class,()->HostTerrain.decode(count));
        byte[] reserved=frame(1);reserved[12]=1;assertThrows(IOException.class,()->HostTerrain.decode(reserved));
        byte[] nan=frame(1,new float[]{Float.NaN,0,0,0,0,10,10,0,0});assertThrows(IOException.class,()->HostTerrain.decode(nan));
        byte[] trailing=new byte[17];assertThrows(IOException.class,()->HostTerrain.decode(trailing));
    }
    @Test void incompleteCoverageNeverEnablesPhysicsAndNearIsBounded() throws IOException {
        byte[] frame=frame(1,new float[]{0,0,0,0,0,10,10,0,0});
        var terrain=HostTerrain.decode(frame);
        assertTrue(terrain.covered(1,0,1,2));assertFalse(terrain.covered(128,0,0,2));
        assertEquals(1,terrain.near(0,-1,0,5,1,5).size());assertTrue(terrain.near(20,-1,20,30,1,30).isEmpty());
        ByteBuffer.wrap(frame).order(ByteOrder.LITTLE_ENDIAN).putInt(8,3);
        assertFalse(HostTerrain.decode(frame).covered(1,0,1,0));
    }
    @Test void degenerateAndInvalidSegmentsNeverHit() throws IOException {
        var terrain=HostTerrain.decode(frame(1,new float[]{0,0,0,0,0,0,0,0,0}));assertEquals(0,terrain.size());
        assertNull(terrain.cast(0,1,0,0,-1,0));
        terrain=HostTerrain.decode(frame(1,new float[]{0,0,0,0,0,10,10,0,0}));
        assertNull(terrain.cast(Double.NaN,1,0,0,-1,0));assertNull(terrain.cast(0,1000,0,0,-1000,0));
        assertNull(terrain.cast(2,1,2,8,1,2));
    }
    @Test void complete256BlockImportIncludesDistantRealGeometryAndRetainsBoundaryGuards() throws IOException {
        byte[] large=frame(6,new float[]{200,0,0,200,0,4,204,0,0});
        ByteBuffer.wrap(large).order(ByteOrder.LITTLE_ENDIAN).putFloat(28,256);
        var terrain=HostTerrain.decode(large);
        assertEquals(256,terrain.radius());assertTrue(terrain.complete());
        for(int x:new int[]{32,64,96,120,200})assertTrue(terrain.covered(x,0,0,2));
        var hit=terrain.cast(201,2,1,201,-2,1);assertNotNull(hit);assertEquals(0,hit.y(),1e-9);
        assertNull(terrain.cast(100,2,1,100,-2,1)); // No invented floor in empty coverage.
        assertFalse(terrain.covered(255,0,0,2));assertFalse(terrain.covered(Double.NaN,0,0,2));
        ByteBuffer.wrap(large).order(ByteOrder.LITTLE_ENDIAN).putInt(8,3);
        assertFalse(HostTerrain.decode(large).covered(32,0,0,2));
        ByteBuffer.wrap(large).order(ByteOrder.LITTLE_ENDIAN).putFloat(28,257);
        assertThrows(IOException.class,()->HostTerrain.decode(large));
    }
    @Test void fluidsSpreadAcrossFloorButCannotDrainThroughIt() throws IOException {
        var floor=HostTerrain.decode(frame(1,
                new float[]{-8,0,-8,-8,0,8,8,0,8},new float[]{-8,0,-8,8,0,8,8,0,-8}));
        assertTrue(floor.blocksFluidTransition(0,0,0,0,-1,0));
        assertFalse(floor.blocksFluidTransition(0,0,0,1,0,0));
        assertFalse(floor.blocksFluidTransition(0,0,0,0,0,-1));
        assertFalse(floor.blocksFluidTransition(0,1,0,0,-1,0));
        assertTrue(floor.blocksFluidTransition(0,-1,0,0,1,0));
    }
    @Test void fluidsRespectUnalignedWallsSlopesAndOpenLedges() throws IOException {
        var wall=HostTerrain.decode(frame(1,
                new float[]{.3f,0,-8,.3f,8,-8,.3f,8,8},new float[]{.3f,0,-8,.3f,8,8,.3f,0,8}));
        assertTrue(wall.blocksFluidTransition(-1,0,0,1,0,0));
        assertTrue(wall.blocksFluidTransition(1,0,0,-1,0,0));
        assertFalse(wall.blocksFluidTransition(-1,0,0,0,0,1));
        var slope=HostTerrain.decode(frame(1,new float[]{0,0,0,0,0,8,8,4,0}));
        assertTrue(slope.blocksFluidTransition(1,1,1,0,-1,0));
        assertFalse(slope.blocksFluidTransition(7,0,7,0,-1,0));
    }
}
