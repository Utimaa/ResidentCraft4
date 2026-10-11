/* Adapted from SkyCraft v0.1.0, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.client.mixin;
import com.llamalad7.mixinextras.injector.wrapoperation.*;
import dev.re4craft.client.HostCollision;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.world.entity.projectile.arrow.AbstractArrow;
import net.minecraft.world.level.*;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
@Mixin(AbstractArrow.class)
public abstract class HostArrowMixin {
    @Inject(method="tick",at=@At("HEAD"),cancellable=true)
    private void re4craft$holdArrow(CallbackInfo ci) {
        if(Re4CraftClient.holdMirrorMovement((AbstractArrow)(Object)this))ci.cancel();
    }
    @WrapOperation(method="tick",at=@At(value="INVOKE",target="Lnet/minecraft/world/level/Level;clipIncludingBorder(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;"))
    private BlockHitResult re4craft$hostArrow(Level level,ClipContext context,Operation<BlockHitResult> original) {
        return HostCollision.refine(context.getFrom(),context.getTo(),original.call(level,context));
    }
}
