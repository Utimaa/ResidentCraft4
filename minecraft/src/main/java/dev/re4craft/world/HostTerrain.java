/* Triangle ray math adapted from SkyCraft v0.1.0 SkyRay/SkyTri, MIT, chasmlol. */
package dev.re4craft.world;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.Collections;
import dev.re4craft.protocol.Wire;

/** Bounded, immutable exact RE4 collision triangles in host coordinates (metres). */
public final class HostTerrain {
    public static final int MAX_TRIANGLES=200000;
    public static final float MAX_RADIUS=256;
    private static final double CELL=2;
    private final int epoch;
    private final List<Triangle> triangles;
    private final Map<Long,List<Triangle>> cells=new HashMap<>();
    private final List<Triangle> broad=new ArrayList<>();
    private final Map<Triangle,HostTriangle> physics=new IdentityHashMap<>();
    private final int flags;
    private final double centerX,centerY,centerZ,radius;
    public record Hit(double t,double x,double y,double z,double nx,double ny,double nz) { }
    public record Triangle(double ax,double ay,double az,double bx,double by,double bz,double cx,double cy,double cz,
                           double nx,double ny,double nz,double minX,double minY,double minZ,double maxX,double maxY,double maxZ) {
        static Triangle create(float[] p) {
            double ux=p[3]-p[0],uy=p[4]-p[1],uz=p[5]-p[2],vx=p[6]-p[0],vy=p[7]-p[1],vz=p[8]-p[2];
            double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,length=Math.sqrt(nx*nx+ny*ny+nz*nz);
            if(length<1e-12)return null;
            return new Triangle(p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7],p[8],nx/length,ny/length,nz/length,
                    Math.min(p[0],Math.min(p[3],p[6])),Math.min(p[1],Math.min(p[4],p[7])),Math.min(p[2],Math.min(p[5],p[8])),
                    Math.max(p[0],Math.max(p[3],p[6])),Math.max(p[1],Math.max(p[4],p[7])),Math.max(p[2],Math.max(p[5],p[8])));
        }
    }
    private HostTerrain(int epoch,int flags,double centerX,double centerY,double centerZ,double radius,List<Triangle> triangles) {
        this.epoch=epoch;this.flags=flags;this.centerX=centerX;this.centerY=centerY;this.centerZ=centerZ;this.radius=radius;this.triangles=List.copyOf(triangles);
        int references=0;
        for(Triangle triangle:triangles) {
            physics.put(triangle,new HostTriangle(new float[]{(float)triangle.ax,(float)triangle.ay,(float)triangle.az,
                    (float)triangle.bx,(float)triangle.by,(float)triangle.bz,(float)triangle.cx,(float)triangle.cy,(float)triangle.cz},0,false));
            int x0=cell(triangle.minX),x1=cell(triangle.maxX),z0=cell(triangle.minZ),z1=cell(triangle.maxZ);
            long count=(long)(x1-x0+1)*(z1-z0+1);
            if(count>64 || references+count>2_000_000) {broad.add(triangle);continue;}
            for(int x=x0;x<=x1;x++)for(int z=z0;z<=z1;z++)cells.computeIfAbsent(key(x,z),ignored->new ArrayList<>()).add(triangle);
            references+=(int)count;
        }
    }
    public int epoch() {return epoch;}
    public int size() {return triangles.size();}
    public double radius() {return radius;}
    public boolean complete() {return (flags&1)!=0 && (flags&2)==0;}
    public boolean covered(double x,double y,double z,double margin) {
        return complete() && finite(x,y,z,margin) && margin>=0 && radius>margin
                && Math.hypot(x-centerX,z-centerZ)<=radius-margin && Math.abs(y-centerY)<=radius-margin;
    }
    public List<HostTriangle> near(double minX,double minY,double minZ,double maxX,double maxY,double maxZ) {
        if(!finite(minX,minY,minZ,maxX,maxY,maxZ) || maxX-minX>256 || maxZ-minZ>256)return List.of();
        Set<Triangle> candidates=Collections.newSetFromMap(new IdentityHashMap<>());candidates.addAll(broad);
        for(int x=cell(minX);x<=cell(maxX);x++)for(int z=cell(minZ);z<=cell(maxZ);z++) {
            var batch=cells.get(key(x,z));if(batch!=null)candidates.addAll(batch);
        }
        List<HostTriangle> output=new ArrayList<>();
        for(Triangle triangle:candidates)if(triangle.maxX>=minX && triangle.minX<=maxX && triangle.maxY>=minY && triangle.minY<=maxY && triangle.maxZ>=minZ && triangle.minZ<=maxZ)output.add(physics.get(triangle));
        return output;
    }
    public static HostTerrain decode(byte[] payload) throws IOException {
        if(payload.length<32 || payload.length>Wire.MAX_PAYLOAD)throw new IOException("Invalid collision payload size");
        ByteBuffer input=ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN);
        int epoch=input.getInt(),count=input.getInt(),flags=input.getInt(),reserved=input.getInt();
        float centerX=input.getFloat(),centerY=input.getFloat(),centerZ=input.getFloat(),radius=input.getFloat();
        if(count<0 || count>MAX_TRIANGLES || payload.length!=32+(long)count*36 || reserved!=0 || (flags&~3)!=0
                || !finite(centerX,centerY,centerZ,radius) || radius<=0 || radius>MAX_RADIUS)
            throw new IOException("Invalid collision count or reserved fields");
        List<Triangle> triangles=new ArrayList<>(count);float[] p=new float[9];
        for(int n=0;n<count;n++) {
            for(int k=0;k<9;k++) {p[k]=input.getFloat();if(!Float.isFinite(p[k]) || Math.abs(p[k])>30_000_000)throw new IOException("Invalid collision coordinate");}
            Triangle triangle=Triangle.create(p);if(triangle!=null)triangles.add(triangle);
        }
        return new HostTerrain(epoch,flags,centerX,centerY,centerZ,radius,triangles);
    }
    public Hit cast(double fx,double fy,double fz,double tx,double ty,double tz) {
        double dx=tx-fx,dy=ty-fy,dz=tz-fz;
        if(!finite(fx,fy,fz,tx,ty,tz) || dx*dx+dy*dy+dz*dz>256*256)return null;
        int x0=cell(Math.min(fx,tx)),x1=cell(Math.max(fx,tx)),z0=cell(Math.min(fz,tz)),z1=cell(Math.max(fz,tz));
        Set<Triangle> candidates=Collections.newSetFromMap(new IdentityHashMap<>());candidates.addAll(broad);
        for(int x=x0;x<=x1;x++)for(int z=z0;z<=z1;z++) {var batch=cells.get(key(x,z));if(batch!=null)candidates.addAll(batch);}
        double best=Double.POSITIVE_INFINITY;Triangle found=null;
        double minX=Math.min(fx,tx),maxX=Math.max(fx,tx),minY=Math.min(fy,ty),maxY=Math.max(fy,ty),minZ=Math.min(fz,tz),maxZ=Math.max(fz,tz);
        for(Triangle triangle:candidates) {
            if(triangle.maxX<minX-1e-7 || triangle.minX>maxX+1e-7 || triangle.maxY<minY-1e-7 || triangle.minY>maxY+1e-7 || triangle.maxZ<minZ-1e-7 || triangle.minZ>maxZ+1e-7)continue;
            double t=intersect(triangle,fx,fy,fz,dx,dy,dz);
            if(t>=0 && t<=1 && t<best) {best=t;found=triangle;}
        }
        if(found==null)return null;
        double sign=found.nx*dx+found.ny*dy+found.nz*dz>0?-1:1;
        return new Hit(best,fx+dx*best,fy+dy*best,fz+dz*best,found.nx*sign,found.ny*sign,found.nz*sign);
    }
    /** Fluid cells retain vanilla levels/ticks; these swept face samples stop
     * neighboring cells from crossing the imported surface, including slopes.
     * Coordinates are the lower corners of two adjacent cells in host metres. */
    public boolean blocksFluidTransition(double x,double y,double z,int dx,int dy,int dz) {
        if(Math.abs(dx)+Math.abs(dy)+Math.abs(dz)!=1)return true;
        double[] samples={.08,.5,.92};
        for(double a:samples)for(double b:samples) {
            double fx=x+(dx==0?a:(dx>0?.001:.999));
            double fy=y+(dy==0?(dx==0?b:a):(dy>0?.001:.999));
            double fz=z+(dz==0?b:(dz>0?.001:.999));
            double tx=fx+dx*1.998,ty=fy+dy*1.998,tz=fz+dz*1.998;
            Hit hit=cast(fx,fy,fz,tx,ty,tz);
            if(hit!=null)return true;
        }
        return false;
    }
    private static double intersect(Triangle p,double ox,double oy,double oz,double dx,double dy,double dz) {
        double e1x=p.bx-p.ax,e1y=p.by-p.ay,e1z=p.bz-p.az,e2x=p.cx-p.ax,e2y=p.cy-p.ay,e2z=p.cz-p.az;
        double px=dy*e2z-dz*e2y,py=dz*e2x-dx*e2z,pz=dx*e2y-dy*e2x,det=e1x*px+e1y*py+e1z*pz;
        if(Math.abs(det)<1e-12)return -1;
        double inv=1/det,sx=ox-p.ax,sy=oy-p.ay,sz=oz-p.az,u=(sx*px+sy*py+sz*pz)*inv;
        if(u< -1e-9 || u>1+1e-9)return -1;
        double qx=sy*e1z-sz*e1y,qy=sz*e1x-sx*e1z,qz=sx*e1y-sy*e1x,v=(dx*qx+dy*qy+dz*qz)*inv;
        if(v< -1e-9 || u+v>1+1e-9)return -1;
        return (e2x*qx+e2y*qy+e2z*qz)*inv;
    }
    private static int cell(double coordinate) {return (int)Math.floor(coordinate/CELL);}
    private static long key(int x,int z) {return ((long)x<<32)|(z&0xffffffffL);}
    private static boolean finite(double... values) {for(double value:values)if(!Double.isFinite(value)||Math.abs(value)>30_000_000)return false;return true;}
}
