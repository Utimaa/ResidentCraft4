package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import net.minecraft.client.server.IntegratedServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Expand actual server tracking while the bridge owns gameplay. Minecraft's
 * TrackedEntity.updatePlayer still applies player view distance, broadcast
 * eligibility and isChunkTracked; no entities or geometry are synthesized. */
@Mixin(IntegratedServer.class)
public abstract class HostEntityTrackingMixin {
    private static final int RE4CRAFT_ENTITY_RANGE=Math.clamp(Integer.getInteger("re4craft.entityRadius",128),16,128);

    @Inject(method="getScaledTrackingDistance",at=@At("RETURN"),cancellable=true)
    private void re4craft$keepDistantEntities(int vanillaRange,CallbackInfoReturnable<Integer> result) {
        if(Re4CraftClient.active() && Re4CraftClient.guestReady())
            result.setReturnValue(Math.max(result.getReturnValueI(),RE4CRAFT_ENTITY_RANGE));
    }
}
