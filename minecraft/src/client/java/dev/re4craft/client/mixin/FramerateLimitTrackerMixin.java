package dev.re4craft.client.mixin;

import com.mojang.blaze3d.platform.FramerateLimitTracker;
import dev.re4craft.client.Re4CraftClient;
import dev.re4craft.client.BackgroundClient;
import dev.re4craft.control.BackgroundClientPolicy;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Keep poses and input responsive when the host owns focus, with a bounded render rate. */
@Mixin(FramerateLimitTracker.class)
public abstract class FramerateLimitTrackerMixin {
    @Inject(method="getFramerateLimit",at=@At("HEAD"),cancellable=true)
    private void re4craft$hostRate(CallbackInfoReturnable<Integer> cir) {
        if(Re4CraftClient.active() || BackgroundClient.enabled())cir.setReturnValue(BackgroundClientPolicy.renderLimit(Re4CraftClient.active()));
    }
}
