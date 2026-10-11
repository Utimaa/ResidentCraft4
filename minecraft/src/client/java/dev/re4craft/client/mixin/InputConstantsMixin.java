package dev.re4craft.client.mixin;

import com.mojang.blaze3d.platform.InputConstants;
import com.mojang.blaze3d.platform.Window;
import dev.re4craft.client.Re4CraftClient;
import dev.re4craft.client.VirtualInput;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.*;

@Mixin(InputConstants.class)
public abstract class InputConstantsMixin {
    @Inject(method="isKeyDown",at=@At("HEAD"),cancellable=true)
    private static void re4craft$virtualKey(int code, CallbackInfoReturnable<Boolean> cir) {
        if (Re4CraftClient.active()) cir.setReturnValue(VirtualInput.isKeyDown(code));
    }
    @Inject(method="grabMouse",at=@At("HEAD"),cancellable=true)
    private static void re4craft$avoidGlobalGrab(Window window,double x,double y,CallbackInfo ci) {
        if (Re4CraftClient.active()) ci.cancel();
    }
}
