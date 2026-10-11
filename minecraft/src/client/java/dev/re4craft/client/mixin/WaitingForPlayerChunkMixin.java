/* Adapted from SkyCraft v0.1.0, copyright (c) 2026 chasmlol, MIT. */
package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** A suppressed/empty world need not compile its player's section before showing the HUD. */
@Mixin(targets="net.minecraft.client.multiplayer.LevelLoadTracker$WaitingForPlayerChunk")
public abstract class WaitingForPlayerChunkMixin {
    @Inject(method="isReady",at=@At("HEAD"),cancellable=true)
    private void re4craft$ready(CallbackInfoReturnable<Boolean> cir) {
        if(Re4CraftClient.active() || Re4CraftClient.mirrorSandbox())cir.setReturnValue(true);
    }
}
