package dev.re4craft.client;

import java.util.EnumSet;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.ai.goal.Goal;
import net.minecraft.world.entity.animal.wolf.Wolf;
import net.minecraft.world.entity.monster.Enemy;

/** Vanilla wolf combat and navigation remain responsible for chasing and biting the target. */
public final class HostWolfTargetGoal extends Goal {
    private final Wolf wolf;
    private LivingEntity selected;
    private int nextScan;
    public HostWolfTargetGoal(Wolf wolf) {this.wolf=wolf;setFlags(EnumSet.of(Flag.TARGET));}
    private boolean hostile(LivingEntity target) {
        if(!HostMobNavigation.ownedWolf(wolf) || wolf.isOrderedToSit() || !Re4CraftClient.guestReady()
                || !HostCollision.covered(wolf.position()) || !HostCollision.covered(target.position())
                || !target.isAlive() || !wolf.canAttack(target))return false;
        var owner=net.minecraft.world.entity.EntityReference.getLivingEntity(wolf.getOwnerReference(),wolf.level());
        if(owner==null || !wolf.wantsToAttack(target,owner))return false;
        return target instanceof HostActorEntity actor?actor.isHostile():target instanceof Enemy;
    }
    @Override public boolean canUse() {
        if(wolf.tickCount<nextScan)return false;
        nextScan=wolf.tickCount+10;
        if(!HostMobNavigation.ownedWolf(wolf)||wolf.isOrderedToSit())return false;
        selected=null;double nearest=24*24;
        for(LivingEntity entity:wolf.level().getEntitiesOfClass(LivingEntity.class,wolf.getBoundingBox().inflate(24),this::hostile)) {
            double distance=wolf.distanceToSqr(entity);
            if(distance<nearest) {nearest=distance;selected=entity;}
        }
        return selected!=null;
    }
    @Override public boolean canContinueToUse() {
        return selected!=null && wolf.getTarget()==selected && hostile(selected) && wolf.distanceToSqr(selected)<32*32;
    }
    @Override public void start() {wolf.setTarget(selected);}
    @Override public void stop() {if(wolf.getTarget()==selected)wolf.setTarget(null);selected=null;}
}
