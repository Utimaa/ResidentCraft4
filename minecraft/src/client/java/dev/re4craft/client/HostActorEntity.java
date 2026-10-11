/* LivingEntity proxy adapted from SkyCraft v0.1.0, copyright (c)2026 chasmlol, MIT. */
package dev.re4craft.client;
import java.util.ArrayDeque;
import java.util.UUID;
import java.util.function.BooleanSupplier;
import dev.re4craft.protocol.Wire;
import net.minecraft.network.syncher.*;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.sounds.SoundEvent;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.*;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;
import org.jspecify.annotations.Nullable;

/** Invisible native actor; vanilla combat determines damage before native transport. */
public class HostActorEntity extends LivingEntity {
    private static final EntityDataAccessor<Integer> ID=SynchedEntityData.defineId(HostActorEntity.class,EntityDataSerializers.INT);
    private static final EntityDataAccessor<Float> WIDTH=SynchedEntityData.defineId(HostActorEntity.class,EntityDataSerializers.FLOAT);
    private static final EntityDataAccessor<Float> HEIGHT=SynchedEntityData.defineId(HostActorEntity.class,EntityDataSerializers.FLOAT);
    private static final EntityDataAccessor<Float> DEPTH=SynchedEntityData.defineId(HostActorEntity.class,EntityDataSerializers.FLOAT);
    private static final EntityDataAccessor<Boolean> BREAKABLE=SynchedEntityData.defineId(HostActorEntity.class,EntityDataSerializers.BOOLEAN);
    private static final EntityDataAccessor<Boolean> HOSTILE=SynchedEntityData.defineId(HostActorEntity.class,EntityDataSerializers.BOOLEAN);
    public record Hit(float damage,float knockback,int flags,Vec3 origin,Vec3 direction,float reach) { }
    private static final class Pending {
        float damage,knockback;int flags;Vec3 origin,direction;float reach;
        Pending(float damage,int flags,Vec3 origin,Vec3 direction,float reach) {
            this.damage=damage;this.flags=flags;this.origin=origin;this.direction=direction;this.reach=reach;
        }
    }
    private final ArrayDeque<Pending> hits=new ArrayDeque<>();
    private record ExplosionHit(DamageSource source,Vec3 center,float reach) { }
    private ExplosionHit explosion;
    public HostActorEntity(EntityType<? extends HostActorEntity> type,Level level) {
        super(type,level);setNoGravity(true);noPhysics=true;setInvisible(true);setSilent(true);
    }
    public int formId() {return entityData.get(ID);}
    public void setFormId(int id) {entityData.set(ID,id);}
    public void setBreakable(boolean value) {entityData.set(BREAKABLE,value);}
    public void setHostile(boolean value) {entityData.set(HOSTILE,value);}
    public boolean isHostile() {return entityData.get(HOSTILE) && !entityData.get(BREAKABLE) && isAlive();}
    @Override protected void defineSynchedData(SynchedEntityData.Builder builder) {
        super.defineSynchedData(builder);builder.define(ID,0);builder.define(WIDTH,.6f);builder.define(HEIGHT,1.8f);builder.define(DEPTH,.6f);builder.define(BREAKABLE,false);builder.define(HOSTILE,false);
    }
    public void setSize(float width,float height,float depth) {
        if(Math.abs(entityData.get(WIDTH)-width)>.01 || Math.abs(entityData.get(HEIGHT)-height)>.01 || Math.abs(entityData.get(DEPTH)-depth)>.01) {
            entityData.set(WIDTH,width);entityData.set(HEIGHT,height);entityData.set(DEPTH,depth);refreshDimensions();
        }
    }
    @Override public void onSyncedDataUpdated(EntityDataAccessor<?> accessor) {
        super.onSyncedDataUpdated(accessor);if(WIDTH.equals(accessor)||HEIGHT.equals(accessor)||DEPTH.equals(accessor))refreshDimensions();
    }
    @Override protected EntityDimensions getDefaultDimensions(Pose pose) {return EntityDimensions.scalable(Math.max(entityData.get(WIDTH),entityData.get(DEPTH)),entityData.get(HEIGHT));}
    /** Vanilla dimensions are square; native attack bounds retain their actual X/Z extents. */
    @Override protected AABB makeBoundingBox(Vec3 position) {
        double width=entityData.get(WIDTH)/2.0,depth=entityData.get(DEPTH)/2.0;
        return new AABB(position.x-width,position.y,position.z-depth,
                position.x+width,position.y+entityData.get(HEIGHT),position.z+depth);
    }
    /** Context exists only during the real server explosion's vanilla hurt invocation. */
    public boolean hurtFromExplosion(DamageSource source,Vec3 center,float reach,BooleanSupplier original) {
        ExplosionHit previous=explosion;
        explosion=Float.isFinite(reach) && reach>0 && reach<=16 && Double.isFinite(center.lengthSqr())
                ?new ExplosionHit(source,center,reach):null;
        try {return original.getAsBoolean();} finally {explosion=previous;}
    }
    @Override protected void actuallyHurt(ServerLevel level,DamageSource source,float damage) {
        UUID player=Re4CraftClient.hostPlayerId();
        if(player==null || !Re4CraftClient.guestReady() || !HostCollision.covered(position())
                || isInvulnerableTo(level,source) || !Float.isFinite(damage) || damage<=0 || hits.size()>=64)return;
        int flags=source.is(net.minecraft.tags.DamageTypeTags.IS_FIRE)?Wire.HIT_FIRE:0;
        // Native breakable props retain their own material immunity and scripts.
        if(entityData.get(BREAKABLE) && flags!=0)return;
        Vec3 origin,direction;float reach;
        if(explosion!=null && explosion.source==source && source.is(net.minecraft.tags.DamageTypeTags.IS_EXPLOSION)) {
            origin=explosion.center;direction=getBoundingBox().getCenter().subtract(origin).normalize();
            reach=explosion.reach;flags|=Wire.HIT_EXPLOSION;
        } else if(source.getDirectEntity() instanceof Projectile projectile) {
            Entity owner=projectile.getOwner();
            if(owner==null || !player.equals(owner.getUUID()) || !(projectile instanceof HostProjectileImpact impact) || impact.re4craft$impact()==null)return;
            origin=impact.re4craft$impact();direction=projectile.getDeltaMovement().normalize();reach=1;flags|=Wire.HIT_PROJECTILE;
        } else if(source.getEntity() instanceof net.minecraft.server.level.ServerPlayer attacker && player.equals(attacker.getUUID())) {
            origin=attacker.getEyePosition();direction=attacker.getLookAngle();reach=(float)attacker.entityInteractionRange();
        } else if(source.getEntity() instanceof net.minecraft.world.entity.animal.wolf.Wolf wolf
                && source.getDirectEntity()==wolf && flags==0 && isHostile()
                && HostMobNavigation.ownedWolf(wolf) && !wolf.isOrderedToSit()
                && HostCollision.covered(wolf.position()) && wolf.isWithinMeleeAttackRange(this)
                && wolf.hasLineOfSight(this)) {
            // Only an actual server-owned tame wolf's vanilla melee hurt invocation is forwarded.
            origin=wolf.getEyePosition();direction=getBoundingBox().getCenter().subtract(origin).normalize();
            reach=3;flags=Wire.HIT_ALLY;
        } else if((flags&Wire.HIT_FIRE)!=0 && source.getEntity()==null && source.getDirectEntity()==null) {
            // Actual server block/fluid contact and the ensuing vanilla fire timer.
            origin=getBoundingBox().getCenter();direction=Vec3.ZERO;reach=1;flags|=Wire.HIT_HAZARD;
        } else return;
        hits.add(new Pending(damage,flags,origin,direction,reach));
        getCombatTracker().recordDamage(source,damage);
    }
    @Override public void knockback(double strength,double xd,double zd,DamageSource source,float damage,boolean effect) {
        Pending hit=hits.peekLast();if(hit!=null && strength>hit.knockback)hit.knockback=(float)strength;
    }
    public void markCritical() {Pending hit=hits.peekLast();if(hit!=null)hit.flags|=1;}
    public Hit takeHit() {
        Pending p=hits.poll();return p==null?null:new Hit(p.damage,p.knockback,p.flags,p.origin,p.direction,p.reach);
    }
    @Override public void tick() {
        // Native snapshots own position; vanilla still owns block contact/fire/cooldowns.
        // Holds preserve a pending fire timer instead of advancing unseen damage.
        if(!Re4CraftClient.guestReady() || !HostCollision.covered(position()))return;
        baseTick();applyEffectsFromBlocks();setHealth(getMaxHealth());
    }
    @Override protected boolean isAffectedByBlocks() {return !isRemoved() && !entityData.get(BREAKABLE);}
    @Override public boolean isPushable() {return false;}
    @Override protected void doPush(Entity entity) { }
    @Override public boolean canBeCollidedWith(@Nullable Entity other) {return false;}
    @Override public boolean shouldShowName() {return false;}
    @Override public boolean shouldBeSaved() {return false;}
    @Override protected @Nullable SoundEvent getHurtSound(DamageSource source) {return null;}
    @Override protected @Nullable SoundEvent getDeathSound() {return null;}
    @Override public HumanoidArm getMainArm() {return HumanoidArm.RIGHT;}
}
