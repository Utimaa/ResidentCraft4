package dev.re4craft.client.mixin;

import dev.re4craft.client.HostMobNavigation;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.navigation.PathNavigation;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(PathNavigation.class)
public abstract class HostPathNavigationMixin {
    @Shadow protected Mob mob;
    @Inject(method="getGroundY",at=@At("RETURN"),cancellable=true)
    private void re4craft$continuousGround(Vec3 destination,CallbackInfoReturnable<Double> cir) {
        if(!HostMobNavigation.applies(mob))return;
        double floor=HostMobNavigation.floor(mob,destination,1.001);
        if(Double.isFinite(floor))cir.setReturnValue(floor);
    }
    @Inject(method="isStableDestination",at=@At("RETURN"),cancellable=true)
    private void re4craft$stableImportedGround(BlockPos pos,CallbackInfoReturnable<Boolean> cir) {
        if(HostMobNavigation.applies(mob) && !cir.getReturnValue()) {
            Vec3 at=Vec3.atBottomCenterOf(pos);double floor=HostMobNavigation.floor(mob,at,1.001);
            cir.setReturnValue(Double.isFinite(floor) && dev.re4craft.client.HostCollision.navigationBody(mob,new Vec3(at.x,floor,at.z)));
        }
    }
}
