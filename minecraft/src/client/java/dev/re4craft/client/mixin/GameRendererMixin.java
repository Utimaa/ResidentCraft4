package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import net.minecraft.client.renderer.GameRenderer;
import org.joml.Vector4f;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Explicit transparent clear prevents a previous full-world frame persisting behind the GUI. */
@Mixin(GameRenderer.class)
public abstract class GameRendererMixin {
    @Inject(method="render",at=@At("HEAD"))
    private void re4craft$transparentClear(CallbackInfo ci) {
        if(Re4CraftClient.active())((GameRenderer)(Object)this).gameRenderState().guiRenderState.clearColorOverride=new Vector4f(0,0,0,0);
    }
}
