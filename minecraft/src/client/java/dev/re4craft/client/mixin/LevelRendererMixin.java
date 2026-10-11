package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import net.minecraft.client.renderer.LevelRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** RE4 supplies the world image. The Minecraft target contains only hands and the real GUI. */
@Mixin(LevelRenderer.class)
public abstract class LevelRendererMixin {
    @Inject(method="render(Lcom/mojang/blaze3d/resource/GraphicsResourceAllocator;ZLnet/minecraft/client/renderer/state/level/CameraRenderState;Lcom/mojang/renderpearl/api/buffers/GpuBufferSlice;Lorg/joml/Vector4f;ZZ)V",
            at=@At("HEAD"),cancellable=true)
    private void re4craft$transparentWorld(CallbackInfo ci) { if(Re4CraftClient.active())ci.cancel(); }
}
