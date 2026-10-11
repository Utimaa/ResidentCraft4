package dev.re4craft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.re4craft.client.HostActorEntity;
import dev.re4craft.client.HostCollision;
import dev.re4craft.client.Re4CraftClient;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.ServerExplosion;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** Real vanilla explosion damage/exposure; imported walls supplement actual Minecraft blocks. */
@Mixin(ServerExplosion.class)
public abstract class HostExplosionMixin {
    @WrapOperation(method="hurtEntities",at=@At(value="INVOKE",target="Lnet/minecraft/world/entity/Entity;hurtServer(Lnet/minecraft/server/level/ServerLevel;Lnet/minecraft/world/damagesource/DamageSource;F)Z"))
    private boolean re4craft$actualBlast(Entity target,ServerLevel level,DamageSource source,float damage,Operation<Boolean> original) {
        if(!(target instanceof HostActorEntity actor))return original.call(target,level,source,damage);
        ServerExplosion explosion=(ServerExplosion)(Object)this;
        return actor.hurtFromExplosion(source,explosion.center(),explosion.radius()*2,
                ()->original.call(target,level,source,damage));
    }

    @WrapOperation(method="getSeenPercent",at=@At(value="INVOKE",target="Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;"))
    private static BlockHitResult re4craft$hostExposure(Level level,ClipContext context,Operation<BlockHitResult> original,Vec3 center,Entity target) {
        BlockHitResult vanilla=original.call(level,context);
        if(!Re4CraftClient.guestReady() || !(target instanceof HostActorEntity || Re4CraftClient.physicsEntity(target))
                || !HostCollision.covered(target.position()) || !HostCollision.covered(center))return vanilla;
        return HostCollision.refine(context.getFrom(),context.getTo(),vanilla);
    }
}
