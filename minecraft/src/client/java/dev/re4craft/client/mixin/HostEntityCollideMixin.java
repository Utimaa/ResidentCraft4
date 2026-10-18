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
    // Stationary NoAI mobs can skip travel/move entirely. Environmental carry
    // belongs to the actual entity tick as well as movement, independent of AI.
    @Inject(method="baseTick",at=@At("HEAD"))
    private void re4craft$carryStationarySurface(CallbackInfo ci) {
        Entity entity=(Entity)(Object)this;HostCollision.carry(entity,entity.getDeltaMovement());
    }
    @Inject(method="baseTick",at=@At("TAIL"))
    private void re4craft$rememberStationarySurface(CallbackInfo ci) {HostCollision.rememberSupport((Entity)(Object)this);}
    @Inject(method="move",at=@At("HEAD"),cancellable=true)
    private void re4craft$holdEmptyMirror(MoverType type,Vec3 movement,CallbackInfo ci) {
        Entity entity=(Entity)(Object)this;
        if(!Re4CraftClient.holdMirrorMovement(entity)){HostCollision.carry(entity,movement);return;}
        if(!(entity instanceof Projectile))entity.setDeltaMovement(Vec3.ZERO);
        ci.cancel();
    }
    @Inject(method="move",at=@At("TAIL"))
    private void re4craft$rememberSurface(MoverType type,Vec3 movement,CallbackInfo ci) {HostCollision.rememberSupport((Entity)(Object)this);}
    @Inject(method="collide",at=@At("RETURN"),cancellable=true)
    private void re4craft$hostTriangles(Vec3 movement,CallbackInfoReturnable<Vec3> cir) {
        cir.setReturnValue(HostCollision.collide((Entity)(Object)this,cir.getReturnValue()));
    }
}
