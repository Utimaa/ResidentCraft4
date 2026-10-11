package dev.re4craft.client.mixin;
import dev.re4craft.client.HostCollision;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;
@Mixin(Player.class)
public abstract class HostPlayerEdgeMixin {
    @Inject(method="maybeBackOffFromEdge",at=@At("HEAD"),cancellable=true)
    private void re4craft$hostSneakEdge(Vec3 delta,MoverType mover,CallbackInfoReturnable<Vec3> cir) {
        Player p=(Player)(Object)this;
        if(Re4CraftClient.guestPlayer(p) && Re4CraftClient.guestReady() && p.isCrouching() && p.onGround()
                && (mover==MoverType.SELF || mover==MoverType.PLAYER))cir.setReturnValue(HostCollision.sneakMove(p,delta));
    }
}
