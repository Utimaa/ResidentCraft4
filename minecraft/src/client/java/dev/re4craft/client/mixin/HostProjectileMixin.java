/* Adapted from SkyCraft v0.1.0, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.client.mixin;
import dev.re4craft.client.HostCollision;
import dev.re4craft.client.HostProjectileImpact;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.entity.projectile.ProjectileDeflection;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.*;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.*;
@Mixin(Projectile.class)
public abstract class HostProjectileMixin implements HostProjectileImpact {
    @Unique private Vec3 re4craft$impact;
    @Shadow protected abstract void onHit(HitResult hit);
    @Override public Vec3 re4craft$impact() {return re4craft$impact;}
    @Inject(method="onHit",at=@At("HEAD"))
    private void re4craft$rememberImpact(HitResult hit,CallbackInfo ci) {re4craft$impact=hit.getLocation();}
    @Inject(method="hitTargetOrDeflectSelf",at=@At("HEAD"),cancellable=true)
    private void re4craft$hitHostAir(HitResult hit,CallbackInfoReturnable<ProjectileDeflection> cir) {
        if(hit instanceof HostCollision.HostHit) {onHit(hit);cir.setReturnValue(ProjectileDeflection.NONE);}
    }
}
