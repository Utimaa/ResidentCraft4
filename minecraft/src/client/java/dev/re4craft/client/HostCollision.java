package dev.re4craft.client;

import dev.re4craft.world.HostTerrain;
import dev.re4craft.world.HostTriangleCollider;
import dev.re4craft.world.HostNavigationGeometry;
import java.io.IOException;
import java.util.List;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;

/** Immutable streamed terrain shared by the client and its integrated server. */
public final class HostCollision {
    private record Frame(HostTerrain terrain,long received) { }
    private static volatile Frame frame;
    private static volatile Frame exhausted;
    private HostCollision() { }
    public static void reset() {frame=null;exhausted=null;}
    public static void accept(byte[] payload) {
        try {
            HostTerrain terrain=HostTerrain.decode(payload);
            boolean first=frame==null || frame.terrain.epoch()!=terrain.epoch();
            frame=new Frame(terrain,System.nanoTime());exhausted=null;
            if(first)Re4CraftClient.LOG.info("RE4 collision frame: epoch {}, {} valid triangles",terrain.epoch(),terrain.size());
        } catch(IOException error) {Re4CraftClient.LOG.warn("Rejected host collision frame: {}",error.getMessage());frame=null;}
    }
    private static Frame current() {
        Frame f=frame;
        return f!=null && f!=exhausted && System.nanoTime()-f.received<5_000_000_000L
                && Re4CraftClient.hostCollisionReady(f.terrain.epoch())?f:null;
    }
    public static boolean covered(Vec3 minecraftFeet) {
        Frame f=current();if(f==null)return false;
        Vec3 p=Re4CraftClient.minecraftToHost(minecraftFeet);
        return f.terrain.covered(p.x,p.y,p.z,2);
    }
    public static String coverageStatus(Vec3 minecraftFeet) {
        Frame f=frame;
        if(f==null)return "no terrain";
        if(!Re4CraftClient.hostCollisionReady(f.terrain.epoch()))return "anchor epoch differs";
        if(System.nanoTime()-f.received>=5_000_000_000L)return "terrain older than 5 seconds";
        if(f==exhausted)return "movement reached terrain coverage boundary";
        Vec3 p=Re4CraftClient.minecraftToHost(minecraftFeet);
        return f.terrain.covered(p.x,p.y,p.z,2)?"complete terrain coverage":"outside complete terrain coverage";
    }
    public static double groundAt(Vec3 minecraftFeet,double above) {
        Frame f=current();if(f==null)return Double.NaN;
        Vec3 p=Re4CraftClient.minecraftToHost(minecraftFeet);
        var tris=f.terrain.near(p.x-1,p.y-4,p.z-1,p.x+1,p.y+above+1,p.z+1);
        double y=HostTriangleCollider.groundAt(tris,p.x,p.y,p.z,above);
        return Double.isNaN(y)?y:Re4CraftClient.hostToMinecraft(p.x,y,p.z).y;
    }
    public static boolean grounded(Vec3 minecraftFeet) {
        Frame f=current();if(f==null)return false;
        Vec3 feet=Re4CraftClient.minecraftToHost(minecraftFeet);
        var support=f.terrain.cast(feet.x,feet.y+0.15,feet.z,feet.x,feet.y-0.15,feet.z);
        return support!=null && support.ny()>0.6 && Math.abs(support.y()-feet.y)<=0.150001;
    }
    /** Exact walkable triangle support within vanilla's bounded step/fall search. */
    public static double navigationGround(Vec3 minecraftFeet,double above,double below) {
        Frame f=current();if(f==null)return Double.NaN;
        Vec3 p=Re4CraftClient.minecraftToHost(minecraftFeet);
        if(!f.terrain.covered(p.x,p.y,p.z,2))return Double.NaN;
        var triangles=f.terrain.near(p.x-1,p.y-below-1,p.z-1,p.x+1,p.y+above+1,p.z+1);
        double y=HostNavigationGeometry.floor(triangles,p.x,p.y,p.z,above,below);
        return Double.isFinite(y)?Re4CraftClient.hostToMinecraft(p.x,y,p.z).y:Double.NaN;
    }
    public static boolean navigationBody(Entity mob,Vec3 minecraftFeet) {
        Frame f=current();if(f==null)return false;
        Vec3 p=Re4CraftClient.minecraftToHost(minecraftFeet);
        if(!f.terrain.covered(p.x,p.y,p.z,2))return false;
        double radius=mob.getBbWidth()/2.0,height=mob.getBbHeight();
        var triangles=f.terrain.near(p.x-radius-.1,p.y-.1,p.z-radius-.1,
                p.x+radius+.1,p.y+height+.1,p.z+radius+.1);
        return HostNavigationGeometry.clearBody(triangles,p.x,p.y,p.z,radius,height,mob.maxUpStep());
    }
    public static boolean navigationEdge(Entity mob,Vec3 from,Vec3 to) {
        Frame f=current();if(f==null)return false;
        Vec3 a=Re4CraftClient.minecraftToHost(from),b=Re4CraftClient.minecraftToHost(to);
        if(!f.terrain.covered(a.x,a.y,a.z,2)||!f.terrain.covered(b.x,b.y,b.z,2))return false;
        double radius=mob.getBbWidth()/2.0,height=mob.getBbHeight(),step=mob.maxUpStep();
        var triangles=f.terrain.near(Math.min(a.x,b.x)-radius-.1,Math.min(a.y,b.y)-1,
                Math.min(a.z,b.z)-radius-.1,Math.max(a.x,b.x)+radius+.1,
                Math.max(a.y,b.y)+height+step+.1,Math.max(a.z,b.z)+radius+.1);
        // The caller also checks intermediate support against actual Minecraft blocks.
        return HostNavigationGeometry.edge(triangles,a.x,a.y,a.z,b.x,b.y,b.z,radius,height,step,false);
    }
    public static boolean sightClear(Vec3 from,Vec3 to) {
        Frame f=current();if(f==null)return false;
        Vec3 a=Re4CraftClient.minecraftToHost(from),b=Re4CraftClient.minecraftToHost(to);
        if(!f.terrain.covered(a.x,a.y,a.z,2)||!f.terrain.covered(b.x,b.y,b.z,2))return false;
        var hit=f.terrain.cast(a.x,a.y,a.z,b.x,b.y,b.z);
        return hit==null || a.distanceToSqr(new Vec3(hit.x(),hit.y(),hit.z()))>=a.distanceToSqr(b)-1e-4;
    }
    /** Minecraft computes velocity and block collision first; the exact host mesh limits it. */
    public static Vec3 collide(Entity player,Vec3 move) {
        if(!Re4CraftClient.physicsEntity(player))return move;
        Frame f=current();
        if(!Re4CraftClient.guestReady() || f==null) {player.setDeltaMovement(Vec3.ZERO);return Vec3.ZERO;}
        if(!Double.isFinite(move.lengthSqr()) || move.lengthSqr()>64*64)return Vec3.ZERO;
        if(Re4CraftClient.guestPlayer(player) && !Re4CraftClient.validCoordinates(player,player.position().add(move))) {
            player.setDeltaMovement(Vec3.ZERO);return Vec3.ZERO;
        }
        Vec3 feet=Re4CraftClient.minecraftToHost(player.position());
        Vec3 target=feet.add(move);
        if(!f.terrain.covered(target.x,target.y,target.z,2)) {
            if(Re4CraftClient.guestPlayer(player))exhausted=f;
            player.setDeltaMovement(Vec3.ZERO);return Vec3.ZERO;
        }
        AABB box=player.getBoundingBox();double step=player.maxUpStep();
        Vec3 lo=Re4CraftClient.minecraftToHost(new Vec3(box.minX,box.minY,box.minZ));
        Vec3 hi=Re4CraftClient.minecraftToHost(new Vec3(box.maxX,box.maxY,box.maxZ));
        var tris=f.terrain.near(Math.min(lo.x,lo.x+move.x)-1,Math.min(lo.y,lo.y+move.y)-1-step,
                Math.min(lo.z,lo.z+move.z)-1,Math.max(hi.x,hi.x+move.x)+1,
                Math.max(hi.y,hi.y+move.y)+1+step,Math.max(hi.z,hi.z+move.z)+1);
        double[] r=HostTriangleCollider.resolve(tris,feet.x,feet.y,feet.z,box.getXsize()/2,
                box.getYsize(),step,player.onGround(),move.x,move.y,move.z);
        if(r[0]==move.x && r[1]==move.y && r[2]==move.z)return move;
        return Entity.collideBoundingBox(player,new Vec3(r[0],r[1],r[2]),box,player.level(),List.of());
    }
    public static Vec3 sneakMove(Entity player,Vec3 move) {
        double x=move.x,z=move.z,step=player.maxUpStep();
        while(x!=0 && !supported(player,new Vec3(x,-step,0)))x=reduce(x);
        while(z!=0 && !supported(player,new Vec3(0,-step,z)))z=reduce(z);
        while(x!=0 && z!=0 && !supported(player,new Vec3(x,-step,z))) {x=reduce(x);z=reduce(z);}
        return new Vec3(x,move.y,z);
    }
    private static double reduce(double x) {return Math.abs(x)<=.05?0:x-Math.copySign(.05,x);}
    private static boolean supported(Entity player,Vec3 offset) {
        if(!player.level().noCollision(player,player.getBoundingBox().move(offset)))return true;
        Vec3 feet=player.position().add(offset.x,0,offset.z);
        double ground=groundAt(feet,.02);
        return Double.isFinite(ground) && feet.y-ground<=player.maxUpStep()+.05;
    }
    public static BlockHitResult refine(Vec3 from,Vec3 to,BlockHitResult vanilla) {
        Frame f=current();if(f==null)return vanilla;
        Vec3 hostFrom=Re4CraftClient.minecraftToHost(from),hostTo=Re4CraftClient.minecraftToHost(to);
        var hit=f.terrain.cast(hostFrom.x,hostFrom.y,hostFrom.z,hostTo.x,hostTo.y,hostTo.z);
        if(hit==null)return vanilla;
        Vec3 location=Re4CraftClient.hostToMinecraft(hit.x(),hit.y(),hit.z());
        if(vanilla.getType()!=HitResult.Type.MISS && from.distanceToSqr(vanilla.getLocation())<=from.distanceToSqr(location))return vanilla;
        Direction face=Direction.getApproximateNearest((float)hit.nx(),(float)hit.ny(),(float)hit.nz());
        BlockPos cell=BlockPos.containing(location.x+0.4*hit.nx(),location.y+0.4*hit.ny(),location.z+0.4*hit.nz());
        return new HostHit(location,face,cell);
    }
    public static final class HostHit extends BlockHitResult {
        public HostHit(Vec3 location,Direction face,BlockPos cell) {super(location,face,cell,false);}
    }
    private record FluidEdge(long cell,int direction) { }
    private static final java.util.concurrent.ConcurrentHashMap<FluidEdge,Boolean> fluidEdges=new java.util.concurrent.ConcurrentHashMap<>();
    private static Frame fluidFrame;
    public static boolean fluidWorld(net.minecraft.world.level.BlockGetter world) {
        if(!(world instanceof net.minecraft.world.level.Level level) || !Re4CraftClient.mirrorSandbox())return false;
        // Hold persisted mirror fluids while the player/anchor is being loaded
        // or replaced on respawn as well as during an ordinary focus pause.
        return level.dimension().equals(net.minecraft.world.level.Level.OVERWORLD);
    }
    public static boolean fluidMayPass(BlockPos from,BlockPos to) {
        Frame f=current();if(f==null || !Re4CraftClient.guestReady())return false;
        Vec3 center=Vec3.atCenterOf(from),target=Vec3.atCenterOf(to);
        if(!covered(center) || !covered(target))return false;
        int dx=to.getX()-from.getX(),dy=to.getY()-from.getY(),dz=to.getZ()-from.getZ();
        if(Math.abs(dx)+Math.abs(dy)+Math.abs(dz)!=1)return false;
        synchronized(fluidEdges) {
            if(fluidFrame!=f) {fluidEdges.clear();fluidFrame=f;}
            FluidEdge key=new FluidEdge(from.asLong(),Direction.getApproximateNearest(dx,dy,dz).ordinal());
            Boolean allowed=fluidEdges.get(key);if(allowed!=null)return allowed;
            Vec3 origin=Re4CraftClient.minecraftToHost(Vec3.atLowerCornerOf(from));
            allowed=!f.terrain.blocksFluidTransition(origin.x,origin.y,origin.z,dx,dy,dz);
            if(fluidEdges.size()<8192)fluidEdges.put(key,allowed);
            return allowed;
        }
    }
}
