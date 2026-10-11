package dev.re4craft.client.mixin;

import dev.re4craft.client.Re4CraftClient;
import net.minecraft.client.player.AbstractClientPlayer;
import net.minecraft.client.Minecraft;
import net.minecraft.core.ClientAsset;
import net.minecraft.resources.Identifier;
import net.minecraft.world.entity.player.PlayerModelType;
import net.minecraft.world.entity.player.PlayerSkin;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.*;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Classic Steve's actual vanilla texture/model also supplies first-person arms while linked. */
@Mixin(AbstractClientPlayer.class)
public abstract class PlayerSkinMixin {
    private static final ClientAsset.ResourceTexture STEVE = new ClientAsset.ResourceTexture(Identifier.withDefaultNamespace("entity/player/wide/steve"));
    @Inject(method="getSkin",at=@At("RETURN"),cancellable=true)
    private void re4craft$steveHands(CallbackInfoReturnable<PlayerSkin> cir) {
        if(!Re4CraftClient.active() || (Object)this!=Minecraft.getInstance().player || Boolean.getBoolean("re4craft.usePlayerSkin"))return;
        PlayerSkin skin=cir.getReturnValue();
        cir.setReturnValue(PlayerSkin.insecure(STEVE,skin.cape(),skin.elytra(),PlayerModelType.WIDE));
    }
}
