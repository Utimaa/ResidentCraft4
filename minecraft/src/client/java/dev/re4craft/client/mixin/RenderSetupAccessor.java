package dev.re4craft.client.mixin;

import java.util.Map;
import net.minecraft.client.renderer.rendertype.RenderSetup;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

@Mixin(RenderSetup.class)
public interface RenderSetupAccessor {
	/** Sampler name -> RenderSetup.TextureBinding (see {@link TextureBindingAccessor}). */
	@Accessor("textures")
	Map<String, ?> re4craft$textures();
}
