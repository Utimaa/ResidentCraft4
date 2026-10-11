package dev.re4craft.client.mixin;

import dev.re4craft.client.HostWolfTargetGoal;
import dev.re4craft.client.HostMobNavigation;
import dev.re4craft.client.HostActorEntity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.TamableAnimal;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.animal.wolf.Wolf;
import net.minecraft.world.level.Level;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(Wolf.class)
public abstract class HostWolfGoalMixin extends TamableAnimal {
    protected HostWolfGoalMixin(EntityType<? extends TamableAnimal> type,Level level) {super(type,level);}
    @Inject(method="registerGoals",at=@At("TAIL"))
    private void re4craft$nativeHostiles(CallbackInfo ci) {
        // Owner defense keeps its original higher priorities; untamed wolves keep vanilla goals.
        targetSelector.addGoal(4,new HostWolfTargetGoal((Wolf)(Object)this));
    }
    @Inject(method="wantsToAttack",at=@At("RETURN"),cancellable=true)
    private void re4craft$friendlyNativeActor(LivingEntity target,LivingEntity owner,CallbackInfoReturnable<Boolean> cir) {
        if(HostMobNavigation.ownedWolf((Wolf)(Object)this) && target instanceof HostActorEntity actor && !actor.isHostile())
            cir.setReturnValue(false);
    }
}
