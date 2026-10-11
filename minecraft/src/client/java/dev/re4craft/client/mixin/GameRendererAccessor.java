package dev.re4craft.client.mixin;
import com.mojang.blaze3d.vertex.PoseStack;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;
@Mixin(GameRenderer.class)
public interface GameRendererAccessor {
    @Invoker("bobView") void re4craft$bobView(CameraRenderState state,PoseStack stack);
    @Invoker("bobHurt") void re4craft$bobHurt(CameraRenderState state,PoseStack stack);
}
