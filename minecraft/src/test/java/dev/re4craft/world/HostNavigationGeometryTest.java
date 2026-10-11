package dev.re4craft.world;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HostNavigationGeometryTest {
    private static void quad(List<HostTriangle> out,float... p) {
        out.add(new HostTriangle(new float[]{p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8]},0,false));
        out.add(new HostTriangle(new float[]{p[0],p[1],p[2],p[6],p[7],p[8],p[9],p[10],p[11]},0,false));
    }
    private static List<HostTriangle> floor() {
        var out=new ArrayList<HostTriangle>();quad(out,-5,0,-5,5,0,-5,5,0,5,-5,0,5);return out;
    }
    @Test void realFloorAndHolesStayDistinct() {
        var t=floor();assertEquals(0,HostNavigationGeometry.floor(t,0,1,0,.01,1.01));
        assertTrue(Double.isNaN(HostNavigationGeometry.floor(t,6,1,0,.01,1.01)));
        assertTrue(Double.isNaN(HostNavigationGeometry.floor(t,0,4,0,.01,2)));
    }
    @Test void boundedFloorSearchDoesNotClassifySteepWallAsWalkable() {
        var t=new ArrayList<HostTriangle>();quad(t,0,0,-2,1,4,-2,1,4,2,0,0,2);
        assertTrue(Double.isNaN(HostNavigationGeometry.floor(t,.5,3,0,1,4)));
    }
    @Test void thinWallRejectsEdgeEvenWhenBothNodeBodiesAreClear() {
        var t=floor();quad(t,0,0,-5,0,0,5,0,3,5,0,3,-5);
        assertTrue(HostNavigationGeometry.clearBody(t,-.5,0,0,.3,1.8,.6));
        assertTrue(HostNavigationGeometry.clearBody(t,.5,0,0,.3,1.8,.6));
        assertFalse(HostNavigationGeometry.edge(t,-.5,0,0,.5,0,0,.3,1.8,.6));
    }
    @Test void lowCeilingRejectsTallMobAndPreservesSmallMob() {
        var t=floor();quad(t,-5,1.3f,-5,5,1.3f,-5,5,1.3f,5,-5,1.3f,5);
        assertFalse(HostNavigationGeometry.clearBody(t,0,0,0,.45,1.4,.6));
        assertTrue(HostNavigationGeometry.clearBody(t,0,0,0,.3,.85,.6));
    }
    @Test void gentleSlopeRemainsAContinuousWalkableEdge() {
        var t=new ArrayList<HostTriangle>();quad(t,-5,-2.5f,-5,5,2.5f,-5,5,2.5f,5,-5,-2.5f,5);
        assertEquals(.25,HostNavigationGeometry.floor(t,.5,1,0,0,1),1e-6);
        assertTrue(HostNavigationGeometry.edge(t,-.5,-.25,0,.5,.25,0,.3,.85,.6));
    }
    @Test void belowStepCeilingStillIntersectsSmallMobBody() {
        var t=floor();quad(t,-5,.4f,-5,5,.4f,-5,5,.4f,5,-5,.4f,5);
        assertFalse(HostNavigationGeometry.clearBody(t,0,0,0,.3,.85,.6));
    }
    @Test void clearEndpointsDoNotPermitAnUnsupportedGap() {
        var t=new ArrayList<HostTriangle>();
        quad(t,-5,0,-5,-.4f,0,-5,-.4f,0,5,-5,0,5);
        quad(t,.4f,0,-5,5,0,-5,5,0,5,.4f,0,5);
        assertTrue(HostNavigationGeometry.clearBody(t,-.5,0,0,.3,.85,.6));
        assertTrue(HostNavigationGeometry.clearBody(t,.5,0,0,.3,.85,.6));
        assertFalse(HostNavigationGeometry.edge(t,-.5,0,0,.5,0,0,.3,.85,.6));
    }
    @Test void rampAboveQuantizedNodeMustTriggerVanillaStepSearch() {
        var t=floor();quad(t,0,0,-5,6,2,-5,6,2,5,0,0,5);
        assertEquals(0,HostNavigationGeometry.floor(t,3.5,1,0,.001,1.001),1e-6);
        assertEquals(7.0/6,HostNavigationGeometry.floor(t,3.5,1,0,.601,0),1e-6);
    }

    @Test void fractionalAndNegativeAnchorsKeepTheStartingNodeOnActualSupport() {
        // Includes the observed live offset: epoch33 MinecraftY56.549437084
        // minus hostY.208760992 gives a floor at guestY56.340676092.
        for(float offset:new float[]{56.340676f,-12.659324f,-12.340676f,63.85f,64f,-64f}) {
            var t=new ArrayList<HostTriangle>();quad(t,-5,offset,-5,5,offset,-5,5,offset,5,-5,offset,5);
            double support=HostNavigationGeometry.floor(t,.5,offset,.5,.15,.15);
            assertEquals(offset,support,1e-6);
            int vanillaStart=(int)Math.floor(support+.5),supportedStart=HostNavigationGeometry.nodeY(support);
            if(vanillaStart<supportedStart) {
                // The old rounded cell cannot see this floor with the normal
                // below-only node search, even though physics supports it.
                assertTrue(Double.isNaN(HostNavigationGeometry.floor(t,.5,vanillaStart,.5,.001,1.001)));
            }
            assertEquals(support,HostNavigationGeometry.floor(t,.5,supportedStart,.5,.001,1.001),1e-6);
            assertTrue(HostNavigationGeometry.clearBody(t,.5,support,.5,.3,.85,.6));
            assertTrue(HostNavigationGeometry.edge(t,.5,support,.5,1.5,support,.5,.3,.85,.6));
        }
    }

    @Test void shiftedSlopeRetainsContinuousEdgesAndCannotFabricateSupport() {
        var t=new ArrayList<HostTriangle>();float offset=-12.340676f;
        quad(t,-5,offset-2.5f,-5,5,offset+2.5f,-5,5,offset+2.5f,5,-5,offset-2.5f,5);
        double from=HostNavigationGeometry.floor(t,-.5,offset,0,.6,.6);
        double to=HostNavigationGeometry.floor(t,.5,offset,0,.6,.6);
        assertTrue(HostNavigationGeometry.edge(t,-.5,from,0,.5,to,0,.3,.85,.6));
        int node=HostNavigationGeometry.nodeY(to);
        assertEquals(to,HostNavigationGeometry.floor(t,.5,node,0,.001,1.001),1e-6);
        assertTrue(Double.isNaN(HostNavigationGeometry.floor(t,6,node,0,.15,1.001)));
        assertThrows(IllegalArgumentException.class,()->HostNavigationGeometry.nodeY(Double.NaN));
    }
}
