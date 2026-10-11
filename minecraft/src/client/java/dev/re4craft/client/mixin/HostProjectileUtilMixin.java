/* Adapted from SkyCraft v0.1.0, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.client.mixin;
import com.llamalad7.mixinextras.injector.wrapoperation.*;
import dev.re4craft.client.HostCollision;
import net.minecraft.world.entity.projectile.ProjectileUtil;
import net.minecraft.world.level.*;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
@Mixin(ProjectileUtil.class)
public abstract class HostProjectileUtilMixin {
    @WrapOperation(method={"getHitResult","getHitEntitiesAlong"},at=@At(value="INVOKE",target="Lnet/minecraft/world/level/Level;clipIncludingBorder(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;"))
    private static BlockHitResult re4craft$hostProjectile(Level level,ClipContext context,Operation<BlockHitResult> original) {
        return HostCollision.refine(context.getFrom(),context.getTo(),original.call(level,context));
    }
}
