package dev.re4craft.client.mixin;

import dev.re4craft.client.BackgroundClient;
import net.minecraft.client.MouseHandler;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Hidden clients must never capture, warp or release the user's real desktop cursor. */
@Mixin(MouseHandler.class)
public abstract class BackgroundMouseMixin {
    @Inject(method={"grabMouse","releaseMouse"},at=@At("HEAD"),cancellable=true)
    private void re4craft$bridgeOwnsMouse(CallbackInfo ci) {if(BackgroundClient.enabled())ci.cancel();}
}
