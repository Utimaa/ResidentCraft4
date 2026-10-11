package dev.re4craft.client.mixin;
import dev.re4craft.client.HostCollision;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
/** SkyCraft MIT adaptation, scoped to the integrated-server bridge player too. */
@Mixin(Entity.class)
public abstract class HostEntityCollideMixin {
    @Inject(method="move",at=@At("HEAD"),cancellable=true)
    private void re4craft$holdEmptyMirror(MoverType type,Vec3 movement,CallbackInfo ci) {
        Entity entity=(Entity)(Object)this;
        if(!Re4CraftClient.holdMirrorMovement(entity))return;
        if(!(entity instanceof Projectile))entity.setDeltaMovement(Vec3.ZERO);
        ci.cancel();
    }
    @Inject(method="collide",at=@At("RETURN"),cancellable=true)
    private void re4craft$hostTriangles(Vec3 movement,CallbackInfoReturnable<Vec3> cir) {
        cir.setReturnValue(HostCollision.collide((Entity)(Object)this,cir.getReturnValue()));
    }
}
