package dev.re4craft.client.mixin;

import dev.re4craft.client.HostMobNavigation;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.navigation.GroundPathNavigation;
import net.minecraft.world.entity.ai.navigation.PathNavigation;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.pathfinder.Path;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(GroundPathNavigation.class)
public abstract class HostGroundNavigationMixin extends PathNavigation {
    protected HostGroundNavigationMixin(Mob mob,Level level) {super(mob,level);}
    @Inject(method="createPath(Lnet/minecraft/core/BlockPos;I)Lnet/minecraft/world/level/pathfinder/Path;",at=@At("HEAD"),cancellable=true)
    private void re4craft$importedDestination(BlockPos target,int reach,CallbackInfoReturnable<Path> cir) {
        if(!HostMobNavigation.applies(mob))return;
        // GroundPathNavigation otherwise scans the empty mirror column to its build limit.
        // The original PathNavigation still performs the actual A* search and constructs Path.
        cir.setReturnValue(super.createPath(HostMobNavigation.destination(mob,target),reach));
    }
}
