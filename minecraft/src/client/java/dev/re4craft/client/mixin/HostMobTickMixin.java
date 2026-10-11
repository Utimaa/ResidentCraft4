package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import net.minecraft.world.entity.Mob;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(Mob.class)
public abstract class HostMobTickMixin {
    @Inject(method="tick",at=@At("HEAD"),cancellable=true)
    private void re4craft$holdUncoveredMob(CallbackInfo ci) {
        Mob mob=(Mob)(Object)this;
        if(!mob.noPhysics && Re4CraftClient.holdMirrorMovement(mob))ci.cancel();
    }
}
