package dev.re4craft.client.mixin;

import dev.re4craft.client.HostCollision;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FlowingFluid;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Keep vanilla fluid source/level/spread logic; imported triangles act as walls. */
@Mixin(FlowingFluid.class)
public abstract class HostFluidMixin {
    @Inject(method="canPassThroughWall",at=@At("RETURN"),cancellable=true)
    private static void re4craft$terrainFace(Direction direction,BlockGetter level,BlockPos from,BlockState fromState,
                                           BlockPos to,BlockState toState,CallbackInfoReturnable<Boolean> cir) {
        if(cir.getReturnValue() && HostCollision.fluidWorld(level) && !HostCollision.fluidMayPass(from,to))cir.setReturnValue(false);
    }
    @Inject(method="tick",at=@At("HEAD"),cancellable=true)
    private void re4craft$holdFluid(ServerLevel level,BlockPos cell,BlockState block,FluidState fluid,CallbackInfo ci) {
        if(HostCollision.fluidWorld(level) && (!Re4CraftClient.guestReady() || !HostCollision.covered(Vec3.atCenterOf(cell)))) {
            level.scheduleTick(cell,fluid.getType(),10);ci.cancel();
        }
    }
}
