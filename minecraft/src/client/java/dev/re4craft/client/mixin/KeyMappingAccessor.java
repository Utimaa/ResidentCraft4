package dev.re4craft.client.mixin;
import net.minecraft.client.KeyMapping;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;
@Mixin(KeyMapping.class)
public interface KeyMappingAccessor {
    @Accessor("clickCount") int re4craft$clickCount();
    @Accessor("clickCount") void re4craft$clickCount(int count);
}
