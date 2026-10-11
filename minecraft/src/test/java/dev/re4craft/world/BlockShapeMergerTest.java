package dev.re4craft.world;

import static org.junit.jupiter.api.Assertions.*;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class BlockShapeMergerTest {
    private static BlockShapeMerger.Box box(double x,double y,double z,double width,double height,double depth) {
        return new BlockShapeMerger.Box(x,y,z,x+width,y+height,z+depth);
    }
    @Test void denseSectionBecomesOneExactBoxWithoutChangingOccupancy() {
        var source=new ArrayList<BlockShapeMerger.Box>();
        for(int x=0;x<16;++x)for(int y=0;y<16;++y)for(int z=0;z<16;++z)source.add(box(x,y,z,1,1,1));
        assertEquals(List.of(box(0,0,0,16,16,16)),BlockShapeMerger.merge(source));
    }
    @Test void slabsAndStairsPreserveHeightAndTheirOpenSpace() {
        var merged=BlockShapeMerger.merge(List.of(box(-2,0,0,1,.5,1),box(-1,0,0,1,.5,1),box(-1,.5,.5,1,.5,.5)));
        assertEquals(2,merged.size());assertTrue(merged.contains(box(-2,0,0,2,.5,1)));
        assertTrue(merged.contains(box(-1,.5,.5,1,.5,.5)));
    }
    @Test void aGapOrDifferentCrossSectionNeverBecomesSolid() {
        assertEquals(2,BlockShapeMerger.merge(List.of(box(0,0,0,1,1,1),box(2,0,0,1,1,1))).size());
        assertEquals(2,BlockShapeMerger.merge(List.of(box(0,0,0,1,1,1),box(1,0,0,1,1,.5))).size());
    }
    @Test void duplicatesOverlapAndNegativeCoordinatesKeepExactUnion() {
        assertEquals(List.of(box(-12,-5,-9,3,1,1)),BlockShapeMerger.merge(List.of(box(-12,-5,-9,2,1,1),box(-11,-5,-9,2,1,1),box(-12,-5,-9,2,1,1))));
    }
    @Test void nativeExtentBoundAndMalformedShapeRemainGuarded() {
        var boxes=new ArrayList<BlockShapeMerger.Box>();for(int x=0;x<32;++x)boxes.add(box(x,0,0,1,1,1));
        assertEquals(2,BlockShapeMerger.merge(boxes).size());
        assertThrows(IllegalArgumentException.class,()->BlockShapeMerger.merge(List.of(box(Double.NaN,0,0,1,1,1))));
        assertThrows(IllegalArgumentException.class,()->BlockShapeMerger.merge(List.of(box(0,0,0,0,1,1))));
    }
}
