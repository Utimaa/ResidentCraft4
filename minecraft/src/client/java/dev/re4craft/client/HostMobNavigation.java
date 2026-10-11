package dev.re4craft.client;

import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.animal.wolf.Wolf;
import net.minecraft.world.entity.ai.navigation.GroundPathNavigation;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.pathfinder.PathType;
import net.minecraft.world.level.pathfinder.WalkNodeEvaluator;
import net.minecraft.world.phys.Vec3;
import dev.re4craft.world.HostNavigationGeometry;

/** Adds the streamed surface to vanilla ground navigation in the owned void mirror. */
public final class HostMobNavigation {
    private HostMobNavigation() { }
    public static boolean applies(Mob mob) {
        return Re4CraftClient.mirrorSandbox() && mob.level().dimension().equals(Level.OVERWORLD)
                && !mob.noPhysics && mob.getNavigation() instanceof GroundPathNavigation;
    }
    public static boolean ownedWolf(Wolf wolf) {
        var owner=wolf.getOwnerReference();
        return applies(wolf) && wolf.isTame() && owner!=null
                && owner.getUUID().equals(Re4CraftClient.hostPlayerId());
    }
    public static Vec3 nodeCenter(Mob mob,int x,double y,int z) {
        double offset=(int)(mob.getBbWidth()+1)*.5;
        return new Vec3(x+offset,y,z+offset);
    }
    public static double floor(Mob mob,Vec3 at,double below) {
        double host=HostCollision.navigationGround(at,.001,below);
        BlockPos cell=BlockPos.containing(at);
        // Retain real vanilla blocks, doors, fluids and entity-specific collision shapes.
        if(!mob.level().getBlockState(cell.below()).isAir()) {
            double block=WalkNodeEvaluator.getFloorLevel(mob.level(),cell);
            if(block<=at.y+.001 && block>=at.y-below && (!Double.isFinite(host)||block>host))host=block;
        }
        return host;
    }
    public static PathType nodeType(Mob mob,int x,int y,int z,PathType vanilla) {
        Vec3 center=nodeCenter(mob,x,y,z);
        if(!Re4CraftClient.guestReady() || !HostCollision.covered(center))return PathType.BLOCKED;
        if(vanilla!=PathType.OPEN && vanilla!=PathType.WALKABLE)return vanilla;
        double higher=HostCollision.navigationGround(center,mob.maxUpStep()+.001,0);
        // OPEN has nonnegative cost, so vanilla descends from it instead of trying
        // the next Y cell. A reachable surface above this quantized node must
        // trigger vanilla's normal tryJumpOn path, including smooth ramp crossings.
        if(Double.isFinite(higher) && higher>center.y+.001)return PathType.BLOCKED;
        double floor=floor(mob,center,1.001);
        if(!Double.isFinite(floor))return vanilla;
        if(HostNavigationGeometry.nodeY(floor)!=y)return vanilla;
        Vec3 feet=new Vec3(center.x,floor,center.z);
        if(!HostCollision.navigationBody(mob,feet))return PathType.BLOCKED;
        return vanilla==PathType.OPEN?PathType.WALKABLE:vanilla;
    }
    public static double nodeFloor(Mob mob,BlockPos node,double vanilla) {
        Vec3 center=nodeCenter(mob,node.getX(),node.getY(),node.getZ());
        double floor=floor(mob,center,1.001);
        return Double.isFinite(floor)?floor:vanilla;
    }
    public static boolean edge(Mob mob,Vec3 from,Vec3 to) {
        double fy=floor(mob,from,Math.max(1,mob.getMaxFallDistance()));
        double ty=floor(mob,to,Math.max(1,mob.getMaxFallDistance()));
        if(!Double.isFinite(fy)||!Double.isFinite(ty))return false;
        if(ty-fy>mob.maxUpStep()+.05 || fy-ty>mob.getMaxFallDistance())return false;
        int samples=Math.max(1,(int)Math.ceil(Math.hypot(to.x-from.x,to.z-from.z)/.2));
        for(int i=1;i<samples;i++) {
            double part=(double)i/samples;
            Vec3 sample=new Vec3(from.x+(to.x-from.x)*part,fy+(ty-fy)*part,from.z+(to.z-from.z)*part);
            if(!Double.isFinite(floor(mob,sample,mob.maxUpStep()+.15)))return false;
        }
        return HostCollision.navigationEdge(mob,new Vec3(from.x,fy,from.z),new Vec3(to.x,ty,to.z));
    }
    public static BlockPos destination(Mob mob,BlockPos desired) {
        Vec3 center=Vec3.atBottomCenterOf(desired);
        double floor=HostCollision.navigationGround(center,mob.maxUpStep()+.001,Math.min(4,mob.getMaxFallDistance()));
        return Double.isFinite(floor)?BlockPos.containing(center.x,HostNavigationGeometry.nodeY(floor),center.z):desired;
    }

    /** Correct only a supported imported starting cell; vanilla still owns A*. */
    public static BlockPos supportedStart(Mob mob,int x,int z) {
        if(!applies(mob) || !Re4CraftClient.guestReady() || mob.isInLiquid() || mob.isPassenger()
                || !HostCollision.covered(mob.position()))return null;
        if(!mob.onGround() && !HostCollision.grounded(mob.position()))return null;
        double support=HostCollision.navigationGround(mob.position(),.15,.15);
        if(!Double.isFinite(support) || Math.abs(support-mob.getY())>.150001)return null;
        Vec3 center=nodeCenter(mob,x,mob.getY(),z);
        double floor=HostCollision.navigationGround(center,mob.maxUpStep()+.15,mob.maxUpStep()+.15);
        if(!Double.isFinite(floor) || Math.abs(floor-support)>mob.maxUpStep()+.05
                || !HostCollision.navigationBody(mob,new Vec3(center.x,floor,center.z)))return null;
        int y=HostNavigationGeometry.nodeY(floor);
        return y>=mob.level().getMinY() && y<mob.level().getMaxY()?new BlockPos(x,y,z):null;
    }
}
