package dev.re4craft.client.mixin;

import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.renderer.LevelRenderer;
import net.minecraft.client.renderer.SubmitNodeCollector;
import net.minecraft.client.renderer.state.level.LevelRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/** Preserve vanilla and Fabric model extraction for the actual breaking stages. */
@Mixin(LevelRenderer.class)
public interface LevelRendererAccessor {
    @Invoker("submitBlockDestroyAnimation")
    void re4craft$submitBlockDestroyAnimation(PoseStack pose,SubmitNodeCollector collector,LevelRenderState state);
}
