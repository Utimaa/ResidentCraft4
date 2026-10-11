package dev.re4craft.client.mixin;

import dev.re4craft.client.HostMobNavigation;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.level.pathfinder.*;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(WalkNodeEvaluator.class)
public abstract class HostWalkNodeMixin extends NodeEvaluator {
    @Shadow protected abstract boolean canStartAt(BlockPos pos);
    @Shadow protected abstract Node getStartNode(BlockPos pos);

    @Inject(method="getStart",at=@At("RETURN"),cancellable=true)
    private void re4craft$supportedFractionalStart(CallbackInfoReturnable<Node> cir) {
        Node original=cir.getReturnValue();if(mob==null || original==null)return;
        // Vanilla rounds grounded feet with floor(y + .5). Imported floors can
        // be fractional after a bridge anchor, while our walkable node is ceil.
        // Reuse vanilla's own cached type/malus rather than creating an A* path
        // or forcing a blocked/fluid cell to become walkable.
        BlockPos supported=HostMobNavigation.supportedStart(mob,original.x,original.z);
        if(supported!=null && supported.getY()!=original.y && canStartAt(supported))
            cir.setReturnValue(getStartNode(supported));
    }
    @Inject(method="getPathTypeOfMob",at=@At("RETURN"),cancellable=true)
    private void re4craft$groundNode(PathfindingContext context,int x,int y,int z,Mob creature,CallbackInfoReturnable<PathType> cir) {
        if(HostMobNavigation.applies(creature))cir.setReturnValue(HostMobNavigation.nodeType(creature,x,y,z,cir.getReturnValue()));
    }
    @Inject(method="getPathTypeStatic(Lnet/minecraft/world/entity/Mob;Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/pathfinder/PathType;",at=@At("RETURN"),cancellable=true)
    private static void re4craft$vanillaStandableDestination(Mob mob,BlockPos pos,CallbackInfoReturnable<PathType> cir) {
        if(HostMobNavigation.applies(mob))cir.setReturnValue(HostMobNavigation.nodeType(mob,pos.getX(),pos.getY(),pos.getZ(),cir.getReturnValue()));
    }
    @Inject(method="getFloorLevel(Lnet/minecraft/core/BlockPos;)D",at=@At("RETURN"),cancellable=true)
    private void re4craft$nodeFloor(BlockPos pos,CallbackInfoReturnable<Double> cir) {
        if(mob!=null && HostMobNavigation.applies(mob))cir.setReturnValue(HostMobNavigation.nodeFloor(mob,pos,cir.getReturnValue()));
    }
    @Inject(method="getNeighbors",at=@At("RETURN"),cancellable=true)
    private void re4craft$edges(Node[] neighbors,Node from,CallbackInfoReturnable<Integer> cir) {
        if(mob==null || !HostMobNavigation.applies(mob))return;
        int accepted=0;
        for(int i=0;i<cir.getReturnValue();i++) {
            Node to=neighbors[i];
            if(HostMobNavigation.edge(mob,HostMobNavigation.nodeCenter(mob,from.x,from.y,from.z),
                    HostMobNavigation.nodeCenter(mob,to.x,to.y,to.z)))neighbors[accepted++]=to;
        }
        cir.setReturnValue(accepted);
    }
}
