package dev.re4craft.client.mixin;

import dev.re4craft.client.HostCollision;
import dev.re4craft.client.HostMobNavigation;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LivingEntity.class)
public abstract class HostMobSightMixin {
    @Inject(method="hasLineOfSight(Lnet/minecraft/world/entity/Entity;)Z",at=@At("RETURN"),cancellable=true)
    private void re4craft$nativeWallSight(Entity target,CallbackInfoReturnable<Boolean> cir) {
        if((Object)this instanceof Mob mob && HostMobNavigation.applies(mob) && cir.getReturnValue())
            cir.setReturnValue(HostCollision.sightClear(mob.getEyePosition(),target.getEyePosition()));
    }
}
