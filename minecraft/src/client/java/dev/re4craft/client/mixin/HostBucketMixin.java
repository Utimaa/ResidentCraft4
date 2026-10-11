package dev.re4craft.client.mixin;

import com.llamalad7.mixinextras.injector.ModifyExpressionValue;
import dev.re4craft.client.HostCollision;
import net.minecraft.world.item.BucketItem;
import net.minecraft.world.level.material.Fluid;
import net.minecraft.world.level.material.Fluids;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.injection.At;

@Mixin(BucketItem.class)
public abstract class HostBucketMixin {
    @Shadow @Final protected Fluid content;
    @ModifyExpressionValue(method="use",at=@At(value="INVOKE",target="Lnet/minecraft/world/item/BucketItem;getPlayerPOVHitResult(Lnet/minecraft/world/level/Level;Lnet/minecraft/world/entity/player/Player;Lnet/minecraft/world/level/ClipContext$Fluid;)Lnet/minecraft/world/phys/BlockHitResult;"))
    private BlockHitResult re4craft$bucketFace(BlockHitResult hit) {
        if(content==Fluids.EMPTY || !(hit instanceof HostCollision.HostHit))return hit;
        // Ordinary block placement uses the empty cell above the surface;
        // BucketItem adds the face itself. Supply its supporting cell instead.
        return new HostCollision.HostHit(hit.getLocation(),hit.getDirection(),hit.getBlockPos().relative(hit.getDirection().getOpposite()));
    }
}
