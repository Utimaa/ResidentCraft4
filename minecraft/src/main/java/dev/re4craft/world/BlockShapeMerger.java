package dev.re4craft.world;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/** Merge only exactly aligned intervals; every output box has the same union
 * as its input voxel boxes. No hulls, gap filling or inferred solid blocks. */
public final class BlockShapeMerger {
    private BlockShapeMerger() { }
    public record Box(double minX,double minY,double minZ,double maxX,double maxY,double maxZ) {
        public double min(int axis) {return axis==0?minX:axis==1?minY:minZ;}
        public double max(int axis) {return axis==0?maxX:axis==1?maxY:maxZ;}
        private Box extend(int axis,double maximum) {
            return new Box(minX,minY,minZ,axis==0?maximum:maxX,axis==1?maximum:maxY,axis==2?maximum:maxZ);
        }
        public boolean valid() {
            for(double value:new double[]{minX,minY,minZ,maxX,maxY,maxZ})if(!Double.isFinite(value))return false;
            return minX<maxX && minY<maxY && minZ<maxZ;
        }
    }
    public static List<Box> merge(List<Box> source) {
        List<Box> result=new ArrayList<>(source);
        for(Box box:result)if(!box.valid())throw new IllegalArgumentException("Invalid collision shape");
        // Two passes allow a staircase of aligned strips to coalesce after
        // the preceding axis pass. Section bounds keep native box extents<=16.
        for(int pass=0;pass<2;++pass)for(int axis=0;axis<3;++axis) {
            final int a=axis,b=(axis+1)%3,c=(axis+2)%3;
            result.sort(Comparator.comparingDouble((Box box)->box.min(b)).thenComparingDouble(box->box.max(b))
                    .thenComparingDouble(box->box.min(c)).thenComparingDouble(box->box.max(c)).thenComparingDouble(box->box.min(a)));
            List<Box> merged=new ArrayList<>(result.size());Box current=null;
            for(Box box:result) {
                if(current!=null && current.min(b)==box.min(b) && current.max(b)==box.max(b)
                        && current.min(c)==box.min(c) && current.max(c)==box.max(c) && box.min(a)<=current.max(a)
                        && Math.max(current.max(a),box.max(a))-current.min(a)<=16) {
                    current=current.extend(a,Math.max(current.max(a),box.max(a)));
                } else {
                    if(current!=null)merged.add(current);current=box;
                }
            }
            if(current!=null)merged.add(current);result=merged;
        }
        return List.copyOf(result);
    }
}
