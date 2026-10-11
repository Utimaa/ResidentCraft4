package dev.re4craft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.re4craft.client.HostCollision;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Item;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** Buckets perform their own vanilla item ray on both the client and server. */
@Mixin(Item.class)
public abstract class HostItemRayMixin {
    @WrapOperation(method="getPlayerPOVHitResult",at=@At(value="INVOKE",target="Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;"))
    private static BlockHitResult re4craft$itemRay(Level level,ClipContext context,Operation<BlockHitResult> original,
                                                  Level originalLevel,Player player,ClipContext.Fluid fluid) {
        BlockHitResult hit=original.call(level,context);
        return Re4CraftClient.guestPlayer(player)?HostCollision.refine(context.getFrom(),context.getTo(),hit):hit;
    }
}
