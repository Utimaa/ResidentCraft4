package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import net.minecraft.client.gui.screens.Screen;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Screen.class)
public abstract class ScreenMixin {
    @Inject(method="isPauseScreen",at=@At("HEAD"),cancellable=true)
    private void re4craft$keepWorldTicking(CallbackInfoReturnable<Boolean> cir) {
        if(Re4CraftClient.active()) cir.setReturnValue(false);
    }
}
