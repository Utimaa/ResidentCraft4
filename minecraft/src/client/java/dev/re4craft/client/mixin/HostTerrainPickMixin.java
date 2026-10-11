package dev.re4craft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.re4craft.client.HostCollision;
import net.minecraft.client.Minecraft;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** Refine only the local player's vanilla crosshair, preserving server/NPC rays. */
@Mixin(Entity.class)
public abstract class HostTerrainPickMixin {
    @WrapOperation(method="pick",at=@At(value="INVOKE",target="Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;"))
    private BlockHitResult re4craft$hostTerrain(Level level,ClipContext context,Operation<BlockHitResult> original) {
        BlockHitResult vanilla=original.call(level,context);
        if((Object)this!=Minecraft.getInstance().player)return vanilla;
        return HostCollision.refine(context.getFrom(),context.getTo(),vanilla);
    }
}
