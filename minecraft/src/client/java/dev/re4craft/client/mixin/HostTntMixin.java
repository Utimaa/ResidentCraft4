package dev.re4craft.client.mixin;

import dev.re4craft.client.HostCollision;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.world.entity.item.PrimedTnt;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** A linked TNT fuse pauses along with its movement when imported terrain is unavailable. */
@Mixin(PrimedTnt.class)
public abstract class HostTntMixin {
    @Inject(method="tick",at=@At("HEAD"),cancellable=true)
    private void re4craft$holdFuse(CallbackInfo ci) {
        PrimedTnt tnt=(PrimedTnt)(Object)this;
        if(Re4CraftClient.physicsEntity(tnt) && (!Re4CraftClient.guestReady() || !HostCollision.covered(tnt.position())))ci.cancel();
    }
}
