package dev.re4craft.client;

import dev.re4craft.protocol.Wire;
import net.minecraft.core.registries.Registries;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.damagesource.DamageTypes;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.phys.Vec3;

/** Preserve vanilla armor, directional blocking, use delay and item durability. */
final class HostDamageBridge {
    static final int FATAL=Wire.DAMAGE_FATAL,HAS_POSITION=Wire.DAMAGE_HAS_POSITION;
    private HostDamageBridge() { }
    static DamageSource source(ServerPlayer player,int kind,int flags,Vec3 position,LivingEntity attacker) {
        var sources=player.level().damageSources();
        // This is the verified native instant-kill branch. It is not an
        // ordinary shieldable mob strike, even if it carries an origin.
        if((flags&FATAL)!=0)return sources.genericKill();
        if(kind!=1)return sources.generic();
        var type=player.level().registryAccess().lookupOrThrow(Registries.DAMAGE_TYPE).getOrThrow(DamageTypes.MOB_ATTACK);
        // The position belongs to this attack, rather than a proxy's later
        // pose or a guessed nearby enemy. hurtServer applies blocking once.
        if((flags&HAS_POSITION)!=0&&position!=null)return new DamageSource(type,position);
        if(attacker!=null&&attacker.isAlive()&&!attacker.isRemoved()&&attacker.level()==player.level())return sources.mobAttack(attacker);
        // Anonymous attacks preserve their actual lack of direction. Vanilla
        // still applies mob armor, but cannot invent a front-facing shield hit.
        return new DamageSource(type);
    }
}
