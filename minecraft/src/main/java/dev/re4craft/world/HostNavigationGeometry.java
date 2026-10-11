package dev.re4craft.world;

import java.util.List;

/** Ground and body queries for vanilla's node evaluator; this does not create paths or blocks. */
public final class HostNavigationGeometry {
    private HostNavigationGeometry() { }

    /** Vanilla nodes are integral; imported feet remain on the exact surface. */
    public static int nodeY(double floor) {
        if(!Double.isFinite(floor))throw new IllegalArgumentException("Missing navigation support");
        return (int)Math.ceil(floor-1e-5);
    }

    public static double floor(List<HostTriangle> triangles,double x,double y,double z,double above,double below) {
        double result=Double.NaN;
        for(HostTriangle triangle:triangles) {
            if(!triangle.walkable)continue;
            double height=triangle.heightAt(x,z);
            if(!Double.isFinite(height) || height>y+above+1e-6 || height<y-below-1e-6)continue;
            if(Double.isNaN(result) || height>result)result=height;
        }
        return result;
    }

    public static boolean clearBody(List<HostTriangle> triangles,double x,double y,double z,
            double radius,double height,double step) {
        double[] resolved=HostTriangleCollider.resolve(triangles,x,y,z,radius,height,step,true,0,0,0);
        if(Math.abs(resolved[0])>1e-5 || Math.abs(resolved[2])>1e-5)return false;
        // A horizontal ceiling through the body does not push its X/Z cylinder.
        for(HostTriangle triangle:triangles) {
            if(triangle.stairHelper || Math.abs(triangle.ny)<.05)continue;
            double center=triangle.heightAt(x,z);
            boolean connectedGround=triangle.walkable && Double.isFinite(center) && center>=y-.2 && center<=y+.15;
            for(int i=0;i<5;i++) {
                double dx=i==1?radius*.7:i==2?-radius*.7:0;
                double dz=i==3?radius*.7:i==4?-radius*.7:0;
                double h=triangle.heightAt(x+dx,z+dz);
                if(!connectedGround && Double.isFinite(h) && h>y+.02 && h<y+height-.02)return false;
            }
        }
        return true;
    }

    /** Reject a thin wall between clear node centers rather than trusting only endpoint boxes. */
    public static boolean edge(List<HostTriangle> triangles,double x,double y,double z,
            double tx,double ty,double tz,double radius,double height,double step) {
        return edge(triangles,x,y,z,tx,ty,tz,radius,height,step,true);
    }
    public static boolean edge(List<HostTriangle> triangles,double x,double y,double z,
            double tx,double ty,double tz,double radius,double height,double step,boolean requireHostGround) {
        int samples=Math.max(1,(int)Math.ceil(Math.hypot(tx-x,tz-z)/.2));
        double px=x,py=y,pz=z;
        for(int i=1;i<=samples;i++) {
            double fraction=(double)i/samples;
            double nx=x+(tx-x)*fraction,ny=y+(ty-y)*fraction,nz=z+(tz-z)*fraction;
            if(requireHostGround && !Double.isFinite(floor(triangles,nx,ny,nz,.001,step+.15)))return false;
            double[] resolved=HostTriangleCollider.resolve(triangles,px,py,pz,radius,height,step,true,
                    nx-px,ny-py,nz-pz);
            if(Math.abs(resolved[0]-(nx-px))>.002 || Math.abs(resolved[2]-(nz-pz))>.002
                    || Math.abs((py+resolved[1])-ny)>Math.max(.15,step+.01))return false;
            px+=resolved[0];py+=resolved[1];pz+=resolved[2];
        }
        return true;
    }
}
